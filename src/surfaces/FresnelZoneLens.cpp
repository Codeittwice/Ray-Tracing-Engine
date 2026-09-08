#include "scrt/surfaces/FresnelZoneLens.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace scrt::surfaces {

FresnelZoneLens::FresnelZoneLens(double focal_length, double inner_radius,
                                 double pitch, int n_zones, double n_lens)
    : focal_length_(focal_length), inner_radius_(inner_radius),
      pitch_(pitch), n_zones_(n_zones), n_lens_(n_lens) {
    rebuild_zones();
}

void FresnelZoneLens::rebuild_zones() {
    sin_alpha_.resize(static_cast<std::size_t>(n_zones_));
    cos_alpha_.resize(static_cast<std::size_t>(n_zones_));

    // Precompute the tilted facet normal for each zone.
    // Derivation: we want optics::refract(d_i=(0,0,-1), n_f, eta=1/n_lens) to produce
    // a ray pointing from (r_mid, 0, 0) toward (0, 0, -focal_length).
    // From Snell's law (vector form), the facet normal direction in the r-z plane is
    //   n_raw = n_lens * d_i - d_t  (unnormalised)
    // where d_i=(0,-1) and d_t=normalise(-r_mid, -f) in r-z coords.
    // This gives: n_raw_r = n_lens * r_mid / L,  n_raw_z = n_lens * f / L - 1.
    for (int i = 0; i < n_zones_; ++i) {
        double r_mid = inner_radius_ + (i + 0.5) * pitch_;
        double L     = std::sqrt(r_mid * r_mid + focal_length_ * focal_length_);
        double nr    = n_lens_ * r_mid / L;
        double nz    = n_lens_ * focal_length_ / L - 1.0;
        double len   = std::sqrt(nr * nr + nz * nz);
        sin_alpha_[static_cast<std::size_t>(i)] = nr / len;
        cos_alpha_[static_cast<std::size_t>(i)] = nz / len;
    }
}

bool FresnelZoneLens::intersect(const core::Ray& r, double t_min, double t_max,
                                core::Hit& hit) const {
    core::Ray lr = xform_.ray_to_local(r);

    // Plane z=0 intersection: t = -oz / dz. As in Plane, the local direction is not
    // renormalised (its length is 1/s under a scale of s), so the parallel test must be
    // the scale-free ratio |d_z| / |d| rather than |d_z| against an absolute epsilon.
    if (std::abs(lr.direction.z) < 1e-14 * glm::length(lr.direction))
        return false;
    double t = -lr.origin.z / lr.direction.z;
    if (t < t_min || t > t_max)
        return false;

    math::vec3 p = lr.origin + t * lr.direction;
    double radius = std::sqrt(p.x * p.x + p.y * p.y);

    // Check annular aperture
    double r_outer = inner_radius_ + n_zones_ * pitch_;
    if (radius < inner_radius_ || radius > r_outer)
        return false;

    // Zone index
    int zone = static_cast<int>((radius - inner_radius_) / pitch_);
    zone = std::clamp(zone, 0, n_zones_ - 1);

    // Tilted facet normal in world space: sin_alpha in radial dir, cos_alpha in +z
    double sa = sin_alpha_[static_cast<std::size_t>(zone)];
    double ca = cos_alpha_[static_cast<std::size_t>(zone)];

    // radius is a local length, so the on-axis guard must be relative to the lens's own
    // size rather than an absolute metre value.
    math::vec3 r_hat;
    if (radius > 1e-14 * outer_radius())
        r_hat = {p.x / radius, p.y / radius, 0.0};
    else
        r_hat = {1.0, 0.0, 0.0};

    math::vec3 n_local = {sa * r_hat.x, sa * r_hat.y, ca};
    // n_local already points toward the incident medium (+z side) by construction.
    bool front = (glm::dot(lr.direction, n_local) < 0.0);
    if (!front) n_local = -n_local;

    hit.t          = t;
    hit.position   = xform_.point_to_world(p);
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {p.x, p.y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB FresnelZoneLens::local_bounds() const {
    // Flat in z on purpose: no z-epsilon, that would change the bounds.
    double r = outer_radius();
    return core::AABB{ {-r, -r, 0.0}, {r, r, 0.0} };
}

void FresnelZoneLens::tessellate(int nseg, std::vector<math::vec3>& verts,
                                 std::vector<std::uint32_t>& indices) const {
    // Flat annular mesh: rings at zone boundaries, nseg segments per ring.
    const int segs  = std::max(nseg * 2, 8);
    const int rings = n_zones_;
    std::uint32_t base = static_cast<std::uint32_t>(verts.size());

    for (int i = 0; i <= rings; ++i) {
        double radius_i = inner_radius_ + i * pitch_;
        for (int j = 0; j <= segs; ++j) {
            double phi = 2.0 * 3.14159265358979323846 * j / segs;
            verts.push_back(xform_.point_to_world(
                {radius_i * std::cos(phi), radius_i * std::sin(phi), 0.0}));
        }
    }

    const std::uint32_t stride = static_cast<std::uint32_t>(segs + 1);
    for (int i = 0; i < rings; ++i) {
        for (int j = 0; j < segs; ++j) {
            std::uint32_t a = base + static_cast<std::uint32_t>(i) * stride + static_cast<std::uint32_t>(j);
            std::uint32_t b = a + 1;
            std::uint32_t c = a + stride;
            std::uint32_t d = c + 1;
            indices.insert(indices.end(), {a, b, c, b, d, c});
        }
    }
}

} // namespace scrt::surfaces
