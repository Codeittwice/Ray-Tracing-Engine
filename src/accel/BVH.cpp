#include "scrt/accel/BVH.hpp"
#include <algorithm>
#include <cassert>
#include <limits>

namespace scrt::accel {

void BVH::build(const std::vector<std::unique_ptr<surfaces::Surface>>& surfaces) {
    // Always clear first: a rebuild with no surfaces must leave the BVH
    // genuinely empty, not retain stale prims_/nodes_ from a previous build
    // (which would otherwise dangle once the old surfaces are freed).
    prims_.clear();
    nodes_.clear();
    if (surfaces.empty()) return;

    prims_.reserve(surfaces.size());
    for (const auto& s : surfaces)
        prims_.push_back(s.get());

    nodes_.reserve(2 * prims_.size());
    build_recursive(0, static_cast<int>(prims_.size()));
}

int BVH::build_recursive(int begin, int end) {
    assert(begin < end);

    int idx = static_cast<int>(nodes_.size());
    nodes_.emplace_back();
    // Deliberately no `Node&` held here across the recursive calls below: nodes_ is indexed
    // (nodes_[idx]) at every access point instead. build() reserves 2*prims_.size() capacity
    // up front, and for a median-split scheme with a leaf threshold of 4 the true worst-case
    // node count is 2*ceil(N/4)-1 (a balanced ~4-ary-leaf tree), well under 2*N, so nodes_
    // never actually reallocates during a build in practice. But relying on that bound to keep
    // a `Node&` alive across recursive build_recursive() calls is exactly the kind of
    // reserve-size assumption that turns into a dangling-reference UB bug the moment the
    // splitting/threshold logic changes; indexing instead of holding a reference makes this
    // function correct regardless of capacity, at no cost.

    // Compute union AABB of all primitives in [begin, end)
    for (int i = begin; i < end; ++i)
        nodes_[idx].aabb.expand(prims_[i]->world_bounds());

    int count = end - begin;
    if (count <= 4) {
        nodes_[idx].first = begin;
        nodes_[idx].count = count;
        return idx;
    }

    // Find longest axis of the centroid bounding box
    core::AABB centroid_bounds;
    for (int i = begin; i < end; ++i)
        centroid_bounds.expand(prims_[i]->world_bounds().centroid());

    math::vec3 extent = centroid_bounds.max() - centroid_bounds.min();
    int axis = 0;
    if (extent[1] > extent[axis]) axis = 1;
    if (extent[2] > extent[axis]) axis = 2;

    // Median split along chosen axis
    int mid = (begin + end) / 2;
    std::nth_element(prims_.begin() + begin, prims_.begin() + mid,
                     prims_.begin() + end,
                     [axis](const surfaces::Surface* a, const surfaces::Surface* b) {
                         return a->world_bounds().centroid()[axis] <
                                b->world_bounds().centroid()[axis];
                     });

    int left  = build_recursive(begin, mid);
    int right = build_recursive(mid, end);
    nodes_[idx].left  = left;
    nodes_[idx].right = right;

    return idx;
}

bool BVH::intersect_node(int idx, const core::Ray& r, double t_min,
                          double& t_max, core::Hit& hit) const {
    const Node& node = nodes_[idx];

    double tn, tf;
    if (!node.aabb.intersect(r, tn, tf)) return false;
    if (tn > t_max || tf < t_min)        return false;

    bool found = false;

    if (node.is_leaf()) {
        core::Hit tmp;
        for (int i = node.first; i < node.first + node.count; ++i) {
            if (prims_[i]->intersect(r, t_min, t_max, tmp)) {
                t_max = tmp.t;
                hit   = tmp;
                found = true;
            }
        }
    } else {
        if (intersect_node(node.left,  r, t_min, t_max, hit)) found = true;
        if (intersect_node(node.right, r, t_min, t_max, hit)) found = true;
    }

    return found;
}

bool BVH::intersect(const core::Ray& r, double t_min, double t_max,
                    core::Hit& hit) const {
    if (nodes_.empty()) return false;
    return intersect_node(0, r, t_min, t_max, hit);
}

} // namespace scrt::accel
