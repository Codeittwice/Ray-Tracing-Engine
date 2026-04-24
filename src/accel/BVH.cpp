#include "scrt/accel/BVH.hpp"
#include <algorithm>
#include <cassert>
#include <limits>

namespace scrt::accel {

void BVH::build(const std::vector<std::unique_ptr<surfaces::Surface>>& surfaces) {
    if (surfaces.empty()) return;

    prims_.clear();
    prims_.reserve(surfaces.size());
    for (const auto& s : surfaces)
        prims_.push_back(s.get());

    nodes_.clear();
    nodes_.reserve(2 * prims_.size());
    build_recursive(0, static_cast<int>(prims_.size()));
}

int BVH::build_recursive(int begin, int end) {
    assert(begin < end);

    int idx = static_cast<int>(nodes_.size());
    nodes_.emplace_back();
    Node& node = nodes_[idx];

    // Compute union AABB of all primitives in [begin, end)
    for (int i = begin; i < end; ++i)
        node.aabb.expand(prims_[i]->world_bounds());

    int count = end - begin;
    if (count <= 4) {
        node.first = begin;
        node.count = count;
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

    node.left  = build_recursive(begin, mid);
    // Re-fetch reference: vector may have reallocated during recursion
    node.right = build_recursive(mid, end);
    nodes_[idx].left  = node.left;
    nodes_[idx].right = node.right;

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
