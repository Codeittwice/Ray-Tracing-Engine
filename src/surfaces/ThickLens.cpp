#include "scrt/surfaces/ThickLens.hpp"
#include "scrt/math/Constants.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace scrt::surfaces {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

/// A closed parametric interval [a, b] along the ray; the set of t where the ray is inside one
/// primitive is a union of at most two of these.
struct Interval {
    double a, b;
};
using Intervals = std::array<Interval, 2>;   // second may be empty (a > b)

Interval empty() { return {1.0, -1.0}; }
bool is_empty(const Interval& i) { return i.a > i.b; }

/// Intersect two interval-unions (each at most two intervals) into at most two intervals.
/// An intersection of two unions of two intervals has at most four pieces, but every
/// primitive here is either one interval or the complement of one, and the running result of
/// intersecting such sets stays at most two pieces; a third piece would be a bug, so refuse it.
Intervals meet(const Intervals& x, const Intervals& y) {
    Intervals out{empty(), empty()};
    int n = 0;
    for (const auto& p : x) {
        if (is_empty(p)) continue;
        for (const auto& q : y) {
            if (is_empty(q)) continue;
            Interval m{std::max(p.a, q.a), std::min(p.b, q.b)};
            if (is_empty(m)) continue;
            if (n < 2) out[static_cast<std::size_t>(n++)] = m;
        }
    }
    return out;
}

/// Solve a*t^2 + 2*bh*t + c = 0; returns false when there is no real root.
bool quadratic(double a, double bh, double c, double& t0, double& t1) {
    const double disc = bh * bh - a * c;
    if (disc < 0.0 || a == 0.0) return false;
    const double s = std::sqrt(disc);
    t0 = (-bh - s) / a;
    t1 = (-bh + s) / a;
    if (t0 > t1) std::swap(t0, t1);
    return true;
}

} // namespace

ThickLens::ThickLens(double radius1, double radius2, double center_thickness_m, double diameter_m)
    : r1_(radius1), r2_(radius2), t_(center_thickness_m), d_(diameter_m) {
    auto bad = [](const std::string& what) { throw std::invalid_argument("ThickLens: " + what); };
    if (!(t_ > 0.0) || !std::isfinite(t_)) bad("center_thickness_m must be finite and > 0");
    if (!(d_ > 0.0) || !std::isfinite(d_)) bad("diameter_m must be finite and > 0");
    for (double r : {r1_, r2_}) {
        if (!std::isfinite(r)) bad("a radius must be finite (0 means flat)");
        if (r != 0.0 && std::abs(r) < 0.5 * d_)
            bad("|radius| " + std::to_string(std::abs(r)) + " is smaller than the half-diameter " +
                std::to_string(0.5 * d_) + ": the face cannot span the aperture");
    }
    if (edge_thickness() < 0.0)
        bad("the faces cross inside the aperture (edge thickness " +
            std::to_string(edge_thickness()) + " m); increase center_thickness_m");
}

double ThickLens::z_front(double rho) const {
    if (r1_ == 0.0) return -0.5 * t_;
    // Sphere centre at z = -t/2 + R1; the face is the part of that sphere nearest the vertex.
    const double sag = r1_ - std::copysign(std::sqrt(r1_ * r1_ - rho * rho), r1_);
    return -0.5 * t_ + sag;
}

double ThickLens::z_back(double rho) const {
    if (r2_ == 0.0) return 0.5 * t_;
    const double sag = r2_ - std::copysign(std::sqrt(r2_ * r2_ - rho * rho), r2_);
    return 0.5 * t_ + sag;
}

bool ThickLens::intersect(const core::Ray& r, double t_min, double t_max, core::Hit& hit) const {
    const core::Ray lr = xform_.ray_to_local(r);
    const math::vec3 o = lr.origin, d = lr.direction;
    const double half_d = 0.5 * d_;

    // Which primitive owns each interval boundary, so the normal can be recovered.
    enum class Prim { Cyl, Face1, Face2, Plane1, Plane2 };
    struct Bound { double t; Prim prim; bool entering; };
    std::array<Bound, 8> bounds{};
    int nb = 0;
    auto note = [&](double t, Prim p, bool entering) {
        if (nb < 8) bounds[static_cast<std::size_t>(nb++)] = {t, p, entering};
    };

    Intervals body{Interval{-kInf, kInf}, empty()};

    // ---- Cylinder r <= D/2 (infinite in z; the faces and planes bound z) ----------------
    {
        const double a = d.x * d.x + d.y * d.y;
        const double bh = o.x * d.x + o.y * d.y;
        const double c = o.x * o.x + o.y * o.y - half_d * half_d;
        double t0, t1;
        if (a < 1e-30) {
            if (c > 0.0) return false;   // parallel to the axis and outside the rim
        } else if (!quadratic(a, bh, c, t0, t1)) {
            return false;
        } else {
            body = meet(body, Intervals{Interval{t0, t1}, empty()});
            note(t0, Prim::Cyl, true);
            note(t1, Prim::Cyl, false);
        }
    }

    // ---- Each face: inside or outside its sphere, or a half-space when flat ---------------
    auto face = [&](double R, double vz, bool is_front) {
        const Prim sphere_prim = is_front ? Prim::Face1 : Prim::Face2;
        const Prim plane_prim  = is_front ? Prim::Plane1 : Prim::Plane2;
        // Body side of the face: for the front face the body is at +z of it, for the back at -z.
        const double side = is_front ? 1.0 : -1.0;
        if (R == 0.0) {
            // Half-space side*(z - vz) >= 0.
            const double f0 = side * (o.z - vz), fd = side * d.z;
            if (std::abs(fd) < 1e-30) {
                if (f0 < 0.0) body = meet(body, Intervals{empty(), empty()});
            } else {
                const double t = -f0 / fd;
                if (fd > 0.0) { body = meet(body, Intervals{Interval{t, kInf}, empty()}); note(t, plane_prim, true); }
                else          { body = meet(body, Intervals{Interval{-kInf, t}, empty()}); note(t, plane_prim, false); }
            }
            return;
        }
        // Sphere centre on the axis at vz + R. Convex toward the outside (R has the sign of
        // `side`) means the body is INSIDE the sphere; concave means OUTSIDE it, plus the
        // half-space beyond the rim edge so the outside-sphere region cannot run off to infinity.
        const double cz = vz + R;
        const math::vec3 oc{o.x, o.y, o.z - cz};
        const double a = glm::dot(d, d), bh = glm::dot(oc, d), c = glm::dot(oc, oc) - R * R;
        double t0, t1;
        const bool convex = (R * side) > 0.0;
        if (convex) {
            if (!quadratic(a, bh, c, t0, t1)) { body = meet(body, Intervals{empty(), empty()}); return; }
            body = meet(body, Intervals{Interval{t0, t1}, empty()});
            note(t0, sphere_prim, true);
            note(t1, sphere_prim, false);
        } else {
            if (quadratic(a, bh, c, t0, t1)) {
                body = meet(body, Intervals{Interval{-kInf, t0}, Interval{t1, kInf}});
                note(t0, sphere_prim, false);
                note(t1, sphere_prim, true);
            }
            // Rim-edge plane: body is on the `side` of z_edge.
            const double z_edge = is_front ? z_front(half_d) : z_back(half_d);
            const double f0 = side * (o.z - z_edge), fd = side * d.z;
            if (std::abs(fd) < 1e-30) {
                if (f0 < 0.0) body = meet(body, Intervals{empty(), empty()});
            } else {
                const double t = -f0 / fd;
                if (fd > 0.0) { body = meet(body, Intervals{Interval{t, kInf}, empty()}); note(t, plane_prim, true); }
                else          { body = meet(body, Intervals{Interval{-kInf, t}, empty()}); note(t, plane_prim, false); }
            }
        }
    };
    face(r1_, -0.5 * t_, true);
    face(r2_, 0.5 * t_, false);

    // ---- The nearest boundary of the body inside [t_min, t_max] -------------------------
    double best = kInf;
    bool entering = false;
    for (const auto& iv : body) {
        if (is_empty(iv)) continue;
        for (double t : {iv.a, iv.b}) {
            if (!std::isfinite(t) || t < t_min || t > t_max) continue;
            if (t < best) { best = t; entering = (t == iv.a); }
        }
    }
    if (!std::isfinite(best)) return false;

    // Recover which primitive produced that boundary (nearest recorded bound to `best`).
    Prim prim = Prim::Cyl;
    double err = kInf;
    for (int i = 0; i < nb; ++i) {
        const auto& b = bounds[static_cast<std::size_t>(i)];
        const double e = std::abs(b.t - best);
        if (e < err) { err = e; prim = b.prim; }
    }

    const math::vec3 p = o + best * d;
    math::vec3 outward;
    switch (prim) {
        case Prim::Cyl:    outward = math::safe_normalize(math::vec3{p.x, p.y, 0.0}); break;
        // A convex face's body lies INSIDE its sphere, so the sphere's own outward normal
        // (p - c)/|R| already points out of the body; a concave face's body lies outside,
        // so the body's outward normal is the sphere's inward one.
        case Prim::Face1:  outward = math::safe_normalize(p - math::vec3{0.0, 0.0, -0.5 * t_ + r1_}) * (r1_ > 0.0 ? 1.0 : -1.0); break;
        case Prim::Face2:  outward = math::safe_normalize(p - math::vec3{0.0, 0.0,  0.5 * t_ + r2_}) * (r2_ < 0.0 ? 1.0 : -1.0); break;
        case Prim::Plane1: outward = {0.0, 0.0, -1.0}; break;
        case Prim::Plane2: outward = {0.0, 0.0,  1.0}; break;
    }
    // Sphere::intersect's convention: the reported normal opposes the incoming ray, and
    // front_face says whether the ray is entering the body. Dielectric reads both.
    const bool front = glm::dot(d, outward) < 0.0;
    (void)entering;
    const math::vec3 n_local = front ? outward : -outward;

    hit.t          = best;
    hit.position   = xform_.point_to_world(p);
    hit.normal     = xform_.normal_to_world(n_local);
    hit.uv         = {p.x, p.y};
    hit.front_face = front;
    hit.surface    = this;
    return true;
}

core::AABB ThickLens::local_bounds() const {
    const double h = 0.5 * d_;
    const double zmin = std::min(z_front(0.0), z_front(h));
    const double zmax = std::max(z_back(0.0), z_back(h));
    return core::AABB{{-h, -h, zmin}, {h, h, zmax}};
}

void ThickLens::tessellate(int nseg, std::vector<math::vec3>& verts,
                           std::vector<std::uint32_t>& indices) const {
    // A lathe: rings of the front face from the axis out, the rim, then the back face in.
    const int around = std::max(12, nseg * 2);
    const int radial = std::max(4, nseg / 2);
    const double h = 0.5 * d_;
    const auto base = static_cast<std::uint32_t>(verts.size());

    auto ring = [&](double rho, double z) {
        for (int j = 0; j < around; ++j) {
            const double a = math::TWO_PI * j / around;
            verts.push_back(xform_.point_to_world({rho * std::cos(a), rho * std::sin(a), z}));
        }
    };
    // Front face: rings from rho = 0 (a degenerate ring) to the rim.
    for (int i = 0; i <= radial; ++i) ring(h * i / radial, z_front(h * i / radial));
    // Back face: rings from the rim inward to the axis.
    for (int i = radial; i >= 0; --i) ring(h * i / radial, z_back(h * i / radial));

    const int rings = 2 * (radial + 1);
    for (int i = 0; i + 1 < rings; ++i) {
        for (int j = 0; j < around; ++j) {
            const auto a = base + static_cast<std::uint32_t>(i * around + j);
            const auto b = base + static_cast<std::uint32_t>(i * around + (j + 1) % around);
            const auto c = base + static_cast<std::uint32_t>((i + 1) * around + j);
            const auto e = base + static_cast<std::uint32_t>((i + 1) * around + (j + 1) % around);
            indices.insert(indices.end(), {a, c, b, b, c, e});
        }
    }
}

} // namespace scrt::surfaces
