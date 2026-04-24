#pragma once
#include "scrt/core/AABB.hpp"
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <memory>
#include <vector>

namespace scrt::accel {

/// Bounding-Volume Hierarchy over Surface pointers (non-owning).
/// Built once via build(); intersect() replaces a linear surface scan.
class BVH {
public:
    BVH() = default;

    /// Build over the supplied surface list. Pointers must remain valid.
    void build(const std::vector<std::unique_ptr<surfaces::Surface>>& surfaces);

    /// Closest-hit query; same semantics as Scene::intersect for the surfaces list.
    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const;

    bool empty() const { return nodes_.empty(); }

private:
    struct Node {
        core::AABB aabb;
        int left  = -1; ///< -1 → leaf node
        int right = -1;
        int first = 0;  ///< index into prims_
        int count = 0;
        bool is_leaf() const noexcept { return left == -1; }
    };

    std::vector<Node>                     nodes_;
    std::vector<surfaces::Surface const*> prims_; ///< reordered during build

    int build_recursive(int begin, int end);
    bool intersect_node(int idx, const core::Ray& r, double t_min, double& t_max,
                        core::Hit& hit) const;
};

} // namespace scrt::accel
