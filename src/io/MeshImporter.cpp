#include "scrt/io/MeshImporter.hpp"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <atomic>
#include <bit>
#include <mutex>
#include <stdexcept>
#include <system_error>
#include <unordered_map>

namespace scrt::io {

namespace {

/// Post-processing flags used for every read of a mesh file, import and inspection alike, so
/// inspect_mesh_file()'s triangle counts and bounds describe exactly what an import produces.
constexpr unsigned int kImportFlags =
    aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_GenNormals;

/// Parses path via Assimp with no caching; the sole cache-fill/import primitive. Never touches
/// the cache — always performs exactly one ReadFile on success or throws on failure.
///
/// `submesh_index` selects one Assimp mesh; kMergedSubmesh merges them all, which is the original
/// (and still default) behaviour — the merged branch iterates exactly as it always did.
ImportedMesh parse_mesh_uncached(const std::filesystem::path& path, double scale_to_meters,
                                 std::size_t submesh_index) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.string(), kImportFlags);

    if (!scene || !scene->HasMeshes())
        throw std::runtime_error("MeshImporter: failed to load " + path.string() +
                                 " — " + importer.GetErrorString());

    if (submesh_index != kMergedSubmesh &&
        submesh_index >= static_cast<std::size_t>(scene->mNumMeshes))
        throw std::runtime_error("MeshImporter: " + path.string() + " has no submesh " +
                                 std::to_string(submesh_index) + " (file holds " +
                                 std::to_string(scene->mNumMeshes) + ")");

    const unsigned int first = (submesh_index == kMergedSubmesh)
                                   ? 0u
                                   : static_cast<unsigned int>(submesh_index);
    const unsigned int last  = (submesh_index == kMergedSubmesh)
                                   ? scene->mNumMeshes
                                   : static_cast<unsigned int>(submesh_index) + 1u;

    ImportedMesh result;
    for (unsigned int m = first; m < last; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];
        std::uint32_t base = static_cast<std::uint32_t>(result.vertices.size());

        for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
            const auto& p = mesh->mVertices[v];
            result.vertices.push_back(
                math::vec3(p.x, p.y, p.z) * scale_to_meters);
        }

        for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
            const aiFace& face = mesh->mFaces[f];
            if (face.mNumIndices != 3) continue;
            result.indices.push_back(base + face.mIndices[0]);
            result.indices.push_back(base + face.mIndices[1]);
            result.indices.push_back(base + face.mIndices[2]);
        }
    }

    if (result.vertices.empty())
        throw std::runtime_error("MeshImporter: " + path.string() + " has no geometry");

    return result;
}

/// Bounding box of a run of Assimp vertices, in raw file units; a vertex-less mesh reports a
/// degenerate box at the origin rather than core::AABB's inside-out default, so a caller that
/// prints extents for every submesh cannot show nonsense for an empty one.
core::AABB raw_bounds_of(const aiMesh* mesh) {
    if (mesh->mNumVertices == 0) return core::AABB{math::vec3(0.0), math::vec3(0.0)};
    core::AABB box;
    for (unsigned int v = 0; v < mesh->mNumVertices; ++v) {
        const auto& p = mesh->mVertices[v];
        box.expand(math::vec3(p.x, p.y, p.z));
    }
    return box;
}

/// Cache key: canonical path + exact bit pattern of the scale factor + the file's last_write_time
/// at the moment of the lookup that created this entry, plus a disambiguating nonce.
///
/// Design note on invalidation: rather than erasing or mutating an existing entry when the file's
/// mtime has moved on (which would mutate/free memory a concurrently-held reference from an
/// earlier import_mesh_cached() call might still be reading — exactly the "evict/overwrite while a
/// reference could be live" hazard the header warns against), a changed mtime simply changes the
/// key. A stale entry from before the on-disk edit is never touched again; it becomes unreachable
/// for future lookups (which now compute a different key) but stays alive and valid in the map,
/// exactly like every other entry, until clear_mesh_cache(). This trades "a file edited many times
/// while old references are still outstanding accumulates orphaned entries" for "no entry is ever
/// mutated or freed out from under a live reference" — the latter is the correctness requirement,
/// the former is an acceptable and documented cost for what is, in practice, an editor-session mesh
/// cache, not a long-running server cache.
///
/// `nonce` handles the case where stat() itself fails (file removed mid-session, permissions,
/// TOCTOU race): rather than inventing a placeholder mtime that could collide with a real one, a
/// stat failure draws a process-wide unique nonce so its key can never collide with — and can never
/// be reused by — any other lookup. Every such call is therefore always a cache miss that always
/// re-parses, which matches "treat a failed stat as stale."
///
/// `submesh` extends the key so that a per-submesh import and the merged import of the same file
/// at the same scale occupy distinct slots. kMergedSubmesh is the merged case, so every lookup
/// that existed before submesh support keeps exactly the key it had, modulo the constant.
struct MeshCacheKey {
    std::filesystem::path           canonical_path;
    std::uint64_t                   scale_bits;
    std::size_t                     submesh; ///< kMergedSubmesh for a merged (whole-file) import.
    std::filesystem::file_time_type mtime;  ///< Meaningful only when nonce == 0.
    std::uint64_t                   nonce;  ///< 0 when mtime came from a successful stat; a
                                             ///< process-wide unique value otherwise.

    bool operator==(const MeshCacheKey& o) const noexcept {
        return scale_bits == o.scale_bits && submesh == o.submesh && nonce == o.nonce &&
               mtime == o.mtime && canonical_path == o.canonical_path;
    }
};

struct MeshCacheKeyHash {
    std::size_t operator()(const MeshCacheKey& k) const noexcept {
        std::size_t h1 = std::filesystem::hash_value(k.canonical_path);
        std::size_t h2 = std::hash<std::uint64_t>{}(k.scale_bits);
        std::size_t h3 = std::hash<std::int64_t>{}(k.mtime.time_since_epoch().count());
        std::size_t h4 = std::hash<std::uint64_t>{}(k.nonce);
        std::size_t h5 = std::hash<std::size_t>{}(k.submesh);
        auto combine = [](std::size_t a, std::size_t b) noexcept {
            return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
        };
        return combine(combine(combine(combine(h1, h2), h3), h4), h5);
    }
};

/// One cache slot: the imported data owned by the map node. Never mutated after insertion —
/// see MeshCacheKey's design note for why (a live reference into `mesh` must never observe a
/// concurrent write).
struct MeshCacheEntry {
    ImportedMesh mesh;
};

/// Process-wide mesh import cache state, guarded by mutex_. unordered_map is node-based, so
/// mapped-value references remain valid across insertions and rehashes (rehashing relocates
/// nodes, not the objects they own — pointers/references to a mapped value survive it); the
/// only operation that can invalidate a previously returned reference is clear_mesh_cache().
struct MeshCache {
    std::mutex                                                          mutex_;
    std::unordered_map<MeshCacheKey, MeshCacheEntry, MeshCacheKeyHash>  entries_;
    MeshCacheStats                                                      stats_;
};

MeshCache& mesh_cache() {
    static MeshCache cache;
    return cache;
}

/// Process-wide source of nonces for lookups whose stat() failed; see MeshCacheKey.
std::atomic<std::uint64_t>& stat_failure_nonce_counter() {
    static std::atomic<std::uint64_t> counter{1}; // 0 is reserved for "stat succeeded"
    return counter;
}

} // namespace

const ImportedMesh& import_submesh_cached(const std::filesystem::path& path,
                                          std::size_t submesh_index, double scale_to_meters) {
    // weakly_canonical (not canonical): resolves as much of the path as exists on disk and
    // lexically normalizes the remainder, without throwing when the target is missing. This
    // keeps a missing/unreadable file on the same error path as before caching existed: it
    // falls through to Assimp's ReadFile, which fails and raises std::runtime_error.
    std::error_code ec;
    std::filesystem::path canonical = std::filesystem::weakly_canonical(path, ec);
    if (ec) canonical = path; // fall back to the raw path; ReadFile below will report the failure

    std::error_code mtime_ec;
    std::filesystem::file_time_type current_write_time =
        std::filesystem::last_write_time(canonical, mtime_ec);

    MeshCacheKey key;
    key.canonical_path = canonical;
    key.scale_bits      = std::bit_cast<std::uint64_t>(scale_to_meters);
    key.submesh         = submesh_index;
    if (!mtime_ec) {
        key.mtime = current_write_time;
        key.nonce = 0;
    } else {
        key.mtime = std::filesystem::file_time_type{};
        key.nonce = stat_failure_nonce_counter().fetch_add(1, std::memory_order_relaxed);
    }

    MeshCache& cache = mesh_cache();
    {
        std::lock_guard<std::mutex> lock(cache.mutex_);
        auto it = cache.entries_.find(key);
        if (it != cache.entries_.end()) {
            ++cache.stats_.hits;
            return it->second.mesh;
        }
    }

    // Parse outside the lock: Assimp can take a long time and never touches cache state. A
    // throwing parse propagates directly out of this function without the cache being touched
    // again, so failures are never cached, per the header contract.
    ImportedMesh fresh = parse_mesh_uncached(canonical, scale_to_meters, submesh_index);

    {
        std::lock_guard<std::mutex> lock(cache.mutex_);
        ++cache.stats_.misses;
        ++cache.stats_.file_reads;
        // try_emplace, not insert_or_assign: if another thread raced us and already inserted
        // this exact key (both threads stat'd the same still-unmodified file concurrently),
        // keep the existing node untouched — never overwrite a slot a reference might already
        // point to — and hand back a reference to that one instead of our own freshly-parsed
        // (now discarded) copy.
        auto [it, inserted] = cache.entries_.try_emplace(std::move(key), MeshCacheEntry{std::move(fresh)});
        return it->second.mesh;
    }
}

const ImportedMesh& import_mesh_cached(const std::filesystem::path& path, double scale_to_meters) {
    return import_submesh_cached(path, kMergedSubmesh, scale_to_meters);
}

ImportedMesh import_mesh(const std::filesystem::path& path, double scale_to_meters) {
    return import_mesh_cached(path, scale_to_meters);
}

ImportedMesh import_submesh(const std::filesystem::path& path, std::size_t submesh_index,
                            double scale_to_meters) {
    return import_submesh_cached(path, submesh_index, scale_to_meters);
}

double unit_scale_to_meters(MeshUnit unit) {
    switch (unit) {
        case MeshUnit::Millimeters: return 1e-3;
        case MeshUnit::Centimeters: return 1e-2;
        case MeshUnit::Meters:      return 1.0;
    }
    return 1.0;
}

const char* mesh_unit_label(MeshUnit unit) {
    switch (unit) {
        case MeshUnit::Millimeters: return "millimetres";
        case MeshUnit::Centimeters: return "centimetres";
        case MeshUnit::Meters:      return "metres";
    }
    return "metres";
}

MeshUnit guess_mesh_unit(double raw_diagonal) {
    if (raw_diagonal > 100.0) return MeshUnit::Millimeters;
    if (raw_diagonal > 3.0)   return MeshUnit::Centimeters;
    return MeshUnit::Meters;
}

MeshFileInfo inspect_mesh_file(const std::filesystem::path& path) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path.string(), kImportFlags);

    if (!scene || !scene->HasMeshes())
        throw std::runtime_error("MeshImporter: failed to load " + path.string() +
                                 " — " + importer.GetErrorString());

    MeshFileInfo info;
    info.path = path;
    info.parts.reserve(scene->mNumMeshes);

    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
        const aiMesh* mesh = scene->mMeshes[m];

        MeshPart part;
        part.index        = m;
        part.name         = mesh->mName.C_Str();
        part.vertex_count = mesh->mNumVertices;
        part.bounds       = raw_bounds_of(mesh);
        for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
            if (mesh->mFaces[f].mNumIndices == 3) ++part.triangle_count;

        info.triangle_count += part.triangle_count;
        info.vertex_count   += part.vertex_count;
        if (part.vertex_count > 0) {
            info.bounds.expand(part.bounds.min());
            info.bounds.expand(part.bounds.max());
        }
        info.parts.push_back(std::move(part));
    }

    if (info.vertex_count == 0)
        throw std::runtime_error("MeshImporter: " + path.string() + " has no geometry");

    info.raw_diagonal = glm::length(info.bounds.max() - info.bounds.min());
    info.guessed_unit = guess_mesh_unit(info.raw_diagonal);
    return info;
}

math::vec3 mesh_centering_translation(const core::AABB& raw_bounds, double scale_to_meters,
                                      const math::vec3& target_center) {
    return target_center - raw_bounds.centroid() * scale_to_meters;
}

std::string scene_relative_mesh_path(const std::filesystem::path& mesh_path,
                                     const std::filesystem::path& scene_dir) {
    std::error_code ec;
    std::filesystem::path rel = std::filesystem::relative(mesh_path, scene_dir, ec);
    if (ec || rel.empty())
        return mesh_path.lexically_normal().generic_string();
    return rel.generic_string();
}

MeshCacheStats mesh_cache_stats() {
    MeshCache& cache = mesh_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);
    return cache.stats_;
}

void clear_mesh_cache() {
    MeshCache& cache = mesh_cache();
    std::lock_guard<std::mutex> lock(cache.mutex_);
    cache.entries_.clear();
    cache.stats_ = MeshCacheStats{};
}

} // namespace scrt::io
