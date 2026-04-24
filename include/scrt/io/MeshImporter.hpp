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
/// Throws std::runtime_error on load failure.
ImportedMesh import_mesh(const std::filesystem::path& path,
                         double scale_to_meters = 1.0);

} // namespace scrt::io
