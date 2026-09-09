#pragma once
#include "scrt/math/Vec.hpp"
#include <cstdint>
#include <filesystem>
#include <vector>

namespace scrt::io {

/// Triangle mesh data returned by import_mesh().
struct ImportedMesh {
    std::vector<math::vec3>    vertices; ///< World-space vertex positions.
    std::vector<std::uint32_t> indices;  ///< Triangle list: every 3 indices = one triangle.
};

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

/// Snapshot of the process-wide mesh import cache's hit/miss/file-read counters.
MeshCacheStats mesh_cache_stats();

/// Drop every entry from the process-wide mesh import cache. Invalidates all references
/// previously returned by import_mesh_cached(); resets mesh_cache_stats() to zero.
void clear_mesh_cache();

} // namespace scrt::io
