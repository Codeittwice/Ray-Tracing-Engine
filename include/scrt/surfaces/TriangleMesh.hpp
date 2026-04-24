#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <cstdint>
#include <vector>

namespace scrt::surfaces {

/// Triangle soup with an internal BVH for fast ray intersection.
class TriangleMesh final : public Surface {
public:
    TriangleMesh(std::vector<math::vec3> vertices,
                 std::vector<std::uint32_t> indices);

    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const override;

    core::AABB world_bounds() const override;

    void tessellate(int nseg, std::vector<math::vec3>& verts,
                    std::vector<std::uint32_t>& indices) const override;

private:
    struct TriNode {
        core::AABB aabb;
        int left  = -1;
        int right = -1;
        int first = 0;
        int count = 0;
        bool is_leaf() const noexcept { return left == -1; }
    };

    std::vector<math::vec3>    verts_;
    std::vector<std::uint32_t> idx_;
    std::vector<int>           tri_order_; ///< indices into idx_/3
    std::vector<TriNode>       tri_nodes_;
    core::AABB                 bounds_;

    int  build_tri(int begin, int end);
    bool isect_tri_node(int node, const core::Ray& r, double t_min,
                        double& t_max, core::Hit& hit) const;
    bool moller_trumbore(int tri_idx, const core::Ray& r, double t_min,
                         double t_max, double& t_hit, math::vec3& n_hit,
                         math::vec2& uv_hit) const;
};

} // namespace scrt::surfaces
