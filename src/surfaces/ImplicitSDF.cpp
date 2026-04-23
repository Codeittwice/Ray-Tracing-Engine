#include "scrt/surfaces/ImplicitSDF.hpp"
#include "scrt/math/Vec.hpp"
#include <cmath>

namespace scrt::surfaces {

ImplicitSDF::ImplicitSDF(Fn f, core::AABB bounds, double hit_eps, int max_iter)
    : fn_(std::move(f)), bounds_(bounds), hit_eps_(hit_eps), max_iter_(max_iter) {}

bool ImplicitSDF::intersect(const core::Ray& r, double t_min, double t_max,
                             core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    // Clip march range to local bounding box.
    double t_enter, t_exit;
    if (!bounds_.intersect(lr, t_enter, t_exit))
        return false;
    double t_start = std::max(t_min,   t_enter);
    double t_end   = std::min(t_max,   t_exit);
    if (t_start >= t_end)
        return false;

    double t_cur  = t_start;
    double d_prev = fn_(lr.origin + t_cur * lr.direction);

    for (int i = 0; i < max_iter_; ++i) {
        math::vec3 p = lr.origin + t_cur * lr.direction;
        double     d = fn_(p);

        if (std::abs(d) < hit_eps_) {
            // Central-difference gradient for outward normal.
            constexpr double eps = 1e-5;
            math::vec3 g{fn_(p + math::vec3{eps,0,0}) - fn_(p - math::vec3{eps,0,0}),
                         fn_(p + math::vec3{0,eps,0}) - fn_(p - math::vec3{0,eps,0}),
                         fn_(p + math::vec3{0,0,eps}) - fn_(p - math::vec3{0,0,eps})};
            double g_len = glm::length(g);
            if (g_len < 1e-14)
                return false;
            math::vec3 outward = g / g_len;
            bool       front   = glm::dot(lr.direction, outward) < 0.0;
            math::vec3 n_local = front ? outward : -outward;

            hit.t          = t_cur;
            hit.position   = xform_.point_to_world(p);
            hit.normal     = xform_.normal_to_world(n_local);
            hit.uv         = {p.x, p.y};
            hit.front_face = front;
            hit.surface    = this;
            return true;
        }

        // Sign change → overshoot; bisect once to get closer.
        if (d * d_prev < 0.0) {
            t_cur  -= d;   // step back by the overshoot amount
            d_prev  = d;
            continue;
        }

        // Advance by the SDF value (sphere-march step).
        t_cur += std::abs(d);
        d_prev = d;

        if (t_cur > t_end)
            return false;
    }

    return false;
}

core::AABB ImplicitSDF::world_bounds() const {
    core::AABB box;
    for (int sx : {0, 1})
        for (int sy : {0, 1})
            for (int sz : {0, 1}) {
                math::vec3 corner{sx ? bounds_.max().x : bounds_.min().x,
                                  sy ? bounds_.max().y : bounds_.min().y,
                                  sz ? bounds_.max().z : bounds_.min().z};
                box.expand(xform_.point_to_world(corner));
            }
    return box;
}

void ImplicitSDF::tessellate(int /*nseg*/, std::vector<math::vec3>& /*verts*/,
                              std::vector<std::uint32_t>& /*indices*/) const {
    // Phase 6: implement marching-cubes tessellation.
}

} // namespace scrt::surfaces
