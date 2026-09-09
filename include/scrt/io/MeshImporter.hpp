#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/math/Vec.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace scrt::io {

/// Triangle mesh data returned by import_mesh().
struct ImportedMesh {
    std::vector<math::vec3>    vertices; ///< World-space vertex positions.
    std::vector<std::uint32_t> indices;  ///< Triangle list: every 3 indices = one triangle.
};

/// Submesh selector meaning "merge every submesh into one buffer" — import_mesh()'s behaviour.
inline constexpr std::size_t kMergedSubmesh = static_cast<std::size_t>(-1);

/// Length unit a mesh file's raw coordinates are expressed in (STL carries no unit of its own).
enum class MeshUnit {
    Millimeters, ///< 1 raw unit = 0.001 m; the house convention for every scene in this repo.
    Centimeters, ///< 1 raw unit = 0.01 m.
    Meters       ///< 1 raw unit = 1 m; the SI-native case.
};

/// Metres per one raw file unit for `unit` (mm -> 1e-3, cm -> 1e-2, m -> 1).
double unit_scale_to_meters(MeshUnit unit);

/// Human-readable label for `unit`, for GUI dropdowns and diagnostics.
const char* mesh_unit_label(MeshUnit unit);

/// Guesses a mesh file's unit from the diagonal of its raw (unscaled) bounding box.
///
/// A 3D file format that carries no unit (STL above all) can only be guessed at from magnitude,
/// and being wrong by 1000x is the most likely way an import silently produces nonsense. The
/// rule is deliberately blunt and physically motivated by the object class this tool imports
/// (solar cookers, tens of centimetres to a few metres across): a raw diagonal above 100 reads
/// as millimetres, above 3 as centimetres, and anything smaller as metres. It is a *default for
/// a UI dropdown*, never an authority — every caller must let the user override it.
MeshUnit guess_mesh_unit(double raw_diagonal);

/// One submesh of a mesh file, described in raw file units (no unit scale applied).
struct MeshPart {
    std::size_t index{0};           ///< Assimp mesh index within the file; valid submesh selector.
    std::string name;               ///< Assimp's mesh name; empty when the format supplies none.
    std::size_t triangle_count{0};  ///< Triangles after triangulation.
    std::size_t vertex_count{0};    ///< Vertices after Assimp's post-processing.
    core::AABB  bounds;             ///< Bounding box in raw file units.
};

/// Everything the import UI needs about a mesh file before a unit has been chosen.
struct MeshFileInfo {
    std::filesystem::path path;           ///< File that was inspected, as supplied by the caller.
    std::vector<MeshPart> parts;          ///< One entry per submesh, in Assimp's order.
    std::size_t           triangle_count{0}; ///< Total triangles across every submesh.
    std::size_t           vertex_count{0};   ///< Total vertices across every submesh.
    core::AABB            bounds;         ///< Union of every submesh's bounds, raw file units.
    double                raw_diagonal{0.0}; ///< Length of `bounds`'s diagonal, raw file units.
    MeshUnit              guessed_unit{MeshUnit::Meters}; ///< guess_mesh_unit(raw_diagonal).
};

/// Reads `path` with Assimp and reports its submeshes and raw bounds without committing to a
/// unit scale; throws std::runtime_error when the file cannot be read or holds no geometry.
///
/// Deliberately NOT cached: it exists so a UI can show what a file contains *before* the user
/// picks a scale, and the import cache is keyed on a scale that is not yet known. It performs
/// exactly one Assimp ReadFile per call, which for a large STL takes on the order of seconds —
/// call it once per file selection, not once per GUI frame, and see the note on
/// import_submesh_cached about moving it off the GUI thread.
MeshFileInfo inspect_mesh_file(const std::filesystem::path& path);

/// Load a mesh file via Assimp; vertices are scaled by scale_to_meters.
/// Throws std::runtime_error on load failure. Implemented as a by-value copy of the
/// process-wide cache entry (see import_mesh_cached), so repeated loads of the same
/// (path, scale) pay one Assimp parse plus a memcpy-shaped vector copy.
ImportedMesh import_mesh(const std::filesystem::path& path,
                         double scale_to_meters = 1.0);

/// Statistics for the process-wide mesh import cache; used by tests and diagnostics.
struct MeshCacheStats {
    std::uint64_t hits{0};        ///< Lookups served from memory.
    std::uint64_t misses{0};      ///< Lookups that had to parse the file.
    std::uint64_t file_reads{0};  ///< Assimp ReadFile calls actually performed.
};

/// Load (or fetch from the process-wide cache) the mesh at path scaled by scale_to_meters,
/// and return a reference to the cache-owned ImportedMesh.
///
/// Cache key: `std::filesystem::weakly_canonical(path)` combined with the exact bit pattern
/// of scale_to_meters (two scales compare equal only if bit-identical; a NaN scale is not a
/// real case for this codebase and is therefore not special-cased — it will simply never
/// compare equal to itself, so a NaN-scaled import always misses and re-parses). We use
/// weakly_canonical rather than canonical because canonical() throws std::filesystem_error
/// when the target file does not exist, which would replace Assimp's "failed to load"
/// std::runtime_error with an unrelated filesystem exception; weakly_canonical resolves as
/// much of the path as exists and lexically normalizes the rest, so a missing file still
/// reaches ReadFile and still fails the way callers (and tests) expect.
///
/// Invalidation: the file's last_write_time (stat'd on every lookup) is folded into the cache
/// key itself, rather than used to evict or overwrite an existing entry in place. When a file's
/// mtime has moved on since it was cached, the new mtime simply produces a different key, so the
/// lookup misses and re-parses into a new slot; the old slot (keyed on the old mtime) is left
/// completely untouched — never mutated, never freed — so a reference returned by an earlier
/// call stays exactly as valid as the contract below promises, even if the file changes on disk
/// in between. The cost is that repeatedly-edited files accumulate stale, unreachable entries
/// until clear_mesh_cache(); this is judged acceptable for an editor-session mesh cache. If the
/// stat() call itself fails (file removed/permission error/TOCTOU race), the lookup is treated
/// as stale unconditionally — it draws a process-wide-unique disambiguator so it can never
/// collide with any other entry, guaranteeing a fresh re-parse — and the resulting ReadFile
/// failure (if any) surfaces through the normal std::runtime_error path. A failed (throwing)
/// import never modifies the cache: no entry is inserted, and nothing already cached is touched.
///
/// Thread safety: a single std::mutex guards the underlying std::unordered_map, held only for
/// the lookup and for the final insert, never across the (potentially slow) Assimp parse. The
/// map is a node-based container, so references to its mapped ImportedMesh values are stable
/// across insertions and rehashes (a rehash relocates nodes, not the objects they own); the
/// returned reference is therefore safe to use after this function's internal lock is released.
/// This is safe ONLY because an entry, once inserted, is never erased, reallocated, or mutated
/// except by clear_mesh_cache() (which invalidates every previously returned reference) — two
/// threads racing to populate the same key resolve via try_emplace, which keeps whichever entry
/// won and discards the other thread's redundant parse rather than overwriting.
const ImportedMesh& import_mesh_cached(const std::filesystem::path& path,
                                       double scale_to_meters = 1.0);

/// Load (or fetch from the process-wide cache) a single submesh of the file at `path`, scaled by
/// scale_to_meters, and return a reference to the cache-owned ImportedMesh.
///
/// `submesh_index` selects one Assimp mesh by index; the sentinel kMergedSubmesh reproduces
/// import_mesh_cached() exactly (every submesh merged into one flat buffer), and in fact
/// import_mesh_cached() is implemented as this function called with that sentinel, so the merged
/// path is the same code and the same cache slot it has always been.
///
/// Splitting matters because a multi-part CAD assembly merged into one surface can only carry one
/// optical material, while a solar cooker's reflector panels and frame want different ones — the
/// existing workaround is splitting the CAD into separate STL files by hand.
///
/// Throws std::runtime_error when the file cannot be read, when `submesh_index` is out of range,
/// or when the selected submesh has no geometry. Cache semantics, thread safety and invalidation
/// are exactly those documented on import_mesh_cached(); the submesh index is simply part of the
/// key, so merged and per-submesh imports of the same file never collide.
const ImportedMesh& import_submesh_cached(const std::filesystem::path& path,
                                          std::size_t submesh_index,
                                          double scale_to_meters = 1.0);

/// By-value copy of the import_submesh_cached() entry for one submesh; see import_mesh().
ImportedMesh import_submesh(const std::filesystem::path& path,
                            std::size_t submesh_index,
                            double scale_to_meters = 1.0);

/// Translation (metres) that puts the centre of `raw_bounds` scaled by `scale_to_meters` at
/// `target_center`; the placement an import uses so a part authored far from its file origin
/// still lands where the user is looking.
math::vec3 mesh_centering_translation(const core::AABB& raw_bounds,
                                      double scale_to_meters,
                                      const math::vec3& target_center);

/// Path text to store in a scene file for `mesh_path`: expressed relative to `scene_dir` with
/// generic '/' separators, falling back to the absolute path when no relative route exists.
///
/// Mesh paths in a scene JSON resolve against the scene file's own directory, which is why the
/// shipped scenes read "../assets/meshes/...". Storing the absolute path an import dialog hands
/// back would break the scene the moment it is saved or moved elsewhere. The fallback covers the
/// cases where a relative route genuinely does not exist — most concretely a different Windows
/// drive letter, where std::filesystem::relative yields an empty path.
std::string scene_relative_mesh_path(const std::filesystem::path& mesh_path,
                                     const std::filesystem::path& scene_dir);

/// Snapshot of the process-wide mesh import cache's hit/miss/file-read counters.
MeshCacheStats mesh_cache_stats();

/// Drop every entry from the process-wide mesh import cache. Invalidates all references
/// previously returned by import_mesh_cached(); resets mesh_cache_stats() to zero.
void clear_mesh_cache();

} // namespace scrt::io
