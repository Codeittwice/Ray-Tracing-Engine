#include "scrt/surfaces/TriangleMesh.hpp"
#include "scrt/math/Constants.hpp"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <numeric>

namespace scrt::surfaces {

TriangleMesh::TriangleMesh(std::vector<math::vec3> vertices,
                           std::vector<std::uint32_t> indices)
    : verts_(std::move(vertices)), idx_(std::move(indices)) {
    assert(idx_.size() % 3 == 0);
    int n = static_cast<int>(idx_.size() / 3);

    // Compute mesh local bounds.
    for (const auto& v : verts_)
        bounds_.expand(v);

    tri_order_.resize(n);
    std::iota(tri_order_.begin(), tri_order_.end(), 0);

    tri_nodes_.reserve(2 * n);
    if (n > 0)
        build_tri(0, n);
}

int TriangleMesh::build_tri(int begin, int end) {
    assert(begin < end);
    int idx = static_cast<int>(tri_nodes_.size());
    tri_nodes_.emplace_back();
    TriNode& node = tri_nodes_[idx];

    for (int i = begin; i < end; ++i) {
        int t = tri_order_[i];
        node.aabb.expand(verts_[idx_[3 * t + 0]]);
        node.aabb.expand(verts_[idx_[3 * t + 1]]);
        node.aabb.expand(verts_[idx_[3 * t + 2]]);
    }

    int count = end - begin;
    if (count <= 4) {
        node.first = begin;
        node.count = count;
        return idx;
    }

    // Longest centroid axis
    core::AABB cb;
    for (int i = begin; i < end; ++i) {
        int t = tri_order_[i];
        math::vec3 c = (verts_[idx_[3*t+0]] + verts_[idx_[3*t+1]] + verts_[idx_[3*t+2]])
                       * (1.0 / 3.0);
        cb.expand(c);
    }
    math::vec3 ext = cb.max() - cb.min();
    int axis = 0;
    if (ext[1] > ext[axis]) axis = 1;
    if (ext[2] > ext[axis]) axis = 2;

    int mid = (begin + end) / 2;
    std::nth_element(tri_order_.begin() + begin,
                     tri_order_.begin() + mid,
                     tri_order_.begin() + end,
                     [&](int a, int b) {
                         math::vec3 ca = (verts_[idx_[3*a+0]] + verts_[idx_[3*a+1]] + verts_[idx_[3*a+2]]) * (1.0/3.0);
                         math::vec3 cb2= (verts_[idx_[3*b+0]] + verts_[idx_[3*b+1]] + verts_[idx_[3*b+2]]) * (1.0/3.0);
                         return ca[axis] < cb2[axis];
                     });

    node.left  = build_tri(begin, mid);
    node.right = build_tri(mid, end);
    tri_nodes_[idx].left  = node.left;
    tri_nodes_[idx].right = node.right;
    return idx;
}

bool TriangleMesh::moller_trumbore(int tri_idx, const core::Ray& r,
                                   double t_min, double t_max,
                                   double& t_hit, math::vec3& n_hit,
                                   math::vec2& uv_hit) const {
    const math::vec3& v0 = verts_[idx_[3 * tri_idx + 0]];
    const math::vec3& v1 = verts_[idx_[3 * tri_idx + 1]];
    const math::vec3& v2 = verts_[idx_[3 * tri_idx + 2]];

    math::vec3 e1 = v1 - v0;
    math::vec3 e2 = v2 - v0;
    math::vec3 h  = glm::cross(r.direction, e2);
    double     a  = glm::dot(e1, h);

    if (std::abs(a) < math::EPSILON_T)
        return false;

    double     f  = 1.0 / a;
    math::vec3 s  = r.origin - v0;
    double     u  = f * glm::dot(s, h);
    if (u < 0.0 || u > 1.0) return false;

    math::vec3 q  = glm::cross(s, e1);
    double     v  = f * glm::dot(r.direction, q);
    if (v < 0.0 || u + v > 1.0) return false;

    double t = f * glm::dot(e2, q);
    if (t < t_min || t > t_max) return false;

    t_hit  = t;
    n_hit  = glm::normalize(glm::cross(e1, e2));
    uv_hit = math::vec2(u, v);
    return true;
}

bool TriangleMesh::isect_tri_node(int nidx, const core::Ray& r,
                                  double t_min, double& t_max,
                                  core::Hit& hit) const {
    const TriNode& node = tri_nodes_[nidx];
    double tn, tf;
    if (!node.aabb.intersect(r, tn, tf)) return false;
    if (tn > t_max || tf < t_min)        return false;

    bool found = false;
    if (node.is_leaf()) {
        for (int i = node.first; i < node.first + node.count; ++i) {
            double t; math::vec3 n; math::vec2 uv;
            if (moller_trumbore(tri_order_[i], r, t_min, t_max, t, n, uv)) {
                t_max        = t;
                hit.t        = t;
                hit.position = r.origin + t * r.direction;
                hit.normal   = (glm::dot(n, r.direction) < 0.0) ? n : -n;
                hit.uv       = uv;
                hit.front_face = glm::dot(n, r.direction) < 0.0;
                hit.surface  = this;
                found = true;
            }
        }
    } else {
        if (isect_tri_node(node.left,  r, t_min, t_max, hit)) found = true;
        if (isect_tri_node(node.right, r, t_min, t_max, hit)) found = true;
    }
    return found;
}

bool TriangleMesh::intersect(const core::Ray& r, double t_min, double t_max,
                              core::Hit& hit) const {
    if (tri_nodes_.empty()) return false;

    core::Ray lr = xform_.ray_to_local(r);
    core::Hit local_hit;
    if (!isect_tri_node(0, lr, t_min, t_max, local_hit))
        return false;

    hit            = local_hit;
    hit.position   = xform_.point_to_world(local_hit.position);
    hit.normal     = xform_.normal_to_world(local_hit.normal);
    hit.surface    = this;
    return true;
}

core::AABB TriangleMesh::world_bounds() const {
    core::AABB box;
    const math::vec3 bmin = bounds_.min();
    const math::vec3 bmax = bounds_.max();
    for (double x : {bmin.x, bmax.x}) {
        for (double y : {bmin.y, bmax.y}) {
            for (double z : {bmin.z, bmax.z}) {
                box.expand(xform_.point_to_world({x, y, z}));
            }
        }
    }
    return box;
}

void TriangleMesh::tessellate(int /*nseg*/, std::vector<math::vec3>& verts,
                               std::vector<std::uint32_t>& indices) const {
    std::uint32_t base = static_cast<std::uint32_t>(verts.size());
    for (const auto& v : verts_)
        verts.push_back(xform_.point_to_world(v));
    for (auto i : idx_)
        indices.push_back(base + i);
}

} // namespace scrt::surfaces
