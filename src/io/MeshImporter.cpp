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

/// Parses path via Assimp with no caching; the sole cache-fill/import primitive. Never touches
/// the cache — always performs exactly one ReadFile on success or throws on failure.
ImportedMesh parse_mesh_uncached(const std::filesystem::path& path, double scale_to_meters) {
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(
        path.string(),
        aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
            aiProcess_GenNormals);

    if (!scene || !scene->HasMeshes())
        throw std::runtime_error("MeshImporter: failed to load " + path.string() +
                                 " — " + importer.GetErrorString());

    ImportedMesh result;
    for (unsigned int m = 0; m < scene->mNumMeshes; ++m) {
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
struct MeshCacheKey {
    std::filesystem::path           canonical_path;
    std::uint64_t                   scale_bits;
    std::filesystem::file_time_type mtime;  ///< Meaningful only when nonce == 0.
    std::uint64_t                   nonce;  ///< 0 when mtime came from a successful stat; a
                                             ///< process-wide unique value otherwise.

    bool operator==(const MeshCacheKey& o) const noexcept {
        return scale_bits == o.scale_bits && nonce == o.nonce && mtime == o.mtime &&
               canonical_path == o.canonical_path;
    }
};

struct MeshCacheKeyHash {
    std::size_t operator()(const MeshCacheKey& k) const noexcept {
        std::size_t h1 = std::filesystem::hash_value(k.canonical_path);
        std::size_t h2 = std::hash<std::uint64_t>{}(k.scale_bits);
        std::size_t h3 = std::hash<std::int64_t>{}(k.mtime.time_since_epoch().count());
        std::size_t h4 = std::hash<std::uint64_t>{}(k.nonce);
        auto combine = [](std::size_t a, std::size_t b) noexcept {
            return a ^ (b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2));
        };
        return combine(combine(combine(h1, h2), h3), h4);
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

const ImportedMesh& import_mesh_cached(const std::filesystem::path& path, double scale_to_meters) {
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
    ImportedMesh fresh = parse_mesh_uncached(canonical, scale_to_meters);

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

ImportedMesh import_mesh(const std::filesystem::path& path, double scale_to_meters) {
    return import_mesh_cached(path, scale_to_meters);
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
