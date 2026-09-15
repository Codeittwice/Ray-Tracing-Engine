#include "scrt/surfaces/SlitPlate.hpp"
#include <cmath>
#include <stdexcept>
#include <string>

namespace scrt::surfaces {

namespace {
void require_pos(double v, const char* what) {
    if (!(v > 0.0) || !std::isfinite(v))
        throw std::invalid_argument(std::string("SlitPlate: ") + what +
                                    " must be finite and > 0 (got " + std::to_string(v) + ")");
}
} // namespace

SlitPlate::SlitPlate(double half_width, double half_height, double slit_width, int slit_count,
                     double slit_pitch)
    : hw_(half_width), hh_(half_height), slit_w_(slit_width), pitch_(slit_pitch),
      count_(slit_count) {
    require_pos(half_width, "half_width");
    require_pos(half_height, "half_height");
    require_pos(slit_width, "slit_width");
    if (slit_count < 1)
        throw std::invalid_argument("SlitPlate: slit_count must be >= 1");
    if (slit_count > 1) {
        require_pos(slit_pitch, "slit_pitch");
        if (slit_pitch <= slit_width)
            throw std::invalid_argument("SlitPlate: slit_pitch must exceed slit_width or the "
                                        "slots merge into one");
    }
}

bool SlitPlate::in_slot(double x) const {
    // Slot i is centred on (i - (count-1)/2) * pitch; find the nearest centre and test.
    const double offset = 0.5 * (count_ - 1) * pitch_;
    double nearest;
    if (count_ == 1) {
        nearest = 0.0;
    } else {
        double k = std::round((x + offset) / pitch_);
        if (k < 0.0) k = 0.0;
        if (k > count_ - 1) k = count_ - 1;
        nearest = k * pitch_ - offset;
    }
    return std::abs(x - nearest) < 0.5 * slit_w_;
}

bool SlitPlate::intersect(const core::Ray& r, double t_min, double t_max,
                          core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);
    if (std::abs(lr.direction.z) < 1e-14 * glm::length(lr.direction))
        return false;

    const double t = -lr.origin.z / lr.direction.z;
    if (t < t_min || t > t_max)
        return false;

    const double x = lr.origin.x + t * lr.direction.x;
    const double y = lr.origin.y + t * lr.direction.y;
    if (std::abs(x) > hw_ || std::abs(y) > hh_)
        return false;
    if (in_slot(x))
        return false;   // through the open slot: the ray continues

    const bool front = (lr.direction.z < 0.0);
    const math::vec3 n_local{0.0, 0.0, front ? 1.0 : -1.0};

    hit.t          = t;
    hit.position   = xform_.point_to_world(math::vec3{x, y, 0.0});
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {x, y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB SlitPlate::local_bounds() const {
    return core::AABB{{-hw_, -hh_, -1e-4}, {hw_, hh_, 1e-4}};
}

void SlitPlate::tessellate(int /*nseg*/, std::vector<math::vec3>& verts,
                           std::vector<std::uint32_t>& indices) const {
    // One rectangle per solid strip between (and beside) the slots, clipped to the plate.
    const double offset = 0.5 * (count_ - 1) * pitch_;
    double x0 = -hw_;
    auto strip = [&](double a, double b) {
        if (b <= a) return;
        const auto base = static_cast<std::uint32_t>(verts.size());
        verts.push_back(xform_.point_to_world({a, -hh_, 0.0}));
        verts.push_back(xform_.point_to_world({b, -hh_, 0.0}));
        verts.push_back(xform_.point_to_world({b,  hh_, 0.0}));
        verts.push_back(xform_.point_to_world({a,  hh_, 0.0}));
        indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
    };
    for (int i = 0; i < count_; ++i) {
        const double c  = i * pitch_ - offset;
        const double lo = std::max(x0, c - 0.5 * slit_w_);
        strip(x0, lo);
        x0 = std::min(hw_, c + 0.5 * slit_w_);
    }
    strip(x0, hw_);
}

} // namespace scrt::surfaces
