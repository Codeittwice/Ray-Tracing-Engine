#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <cstdint>
#include <functional>
#include <vector>

namespace scrt::surfaces {

/// Implicit surface defined by a signed-distance function (SDF) in local space.
/// Intersection via sphere marching; normal via central differences.
class ImplicitSDF final : public Surface {
public:
    using Fn = std::function<double(math::vec3)>;

    /// @param f        SDF: positive outside, negative inside, zero on surface.
    /// @param bounds   Local-space bounding box; rays outside skip this surface.
    /// @param hit_eps  Convergence threshold (m); default 1 µm.
    /// @param max_iter Maximum sphere-march steps before declaring a miss.
    ImplicitSDF(Fn f, core::AABB bounds, double hit_eps = 1e-6, int max_iter = 512);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;
    core::AABB world_bounds() const override;
    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

private:
    Fn         fn_;
    core::AABB bounds_;
    double     hit_eps_;
    int        max_iter_;
};

} // namespace scrt::surfaces
