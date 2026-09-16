#include "scrt/optics/Polarisation.hpp"

#include <cmath>

namespace scrt::optics {

math::vec3 p_axis(const core::Ray& r) noexcept {
    return glm::cross(r.direction, r.s_axis);
}

FieldVec world_field(const core::Ray& r) noexcept {
    const math::vec3 s = r.s_axis;
    const math::vec3 p = p_axis(r);
    return {r.Es * s.x + r.Ep * p.x, r.Es * s.y + r.Ep * p.y, r.Es * s.z + r.Ep * p.z};
}

void align_to_interface(core::Ray& r, math::vec3 n) noexcept {
    const math::vec3 c   = glm::cross(r.direction, n);
    const double     len = glm::length(c);
    if (len < 1e-9) return;   // normal incidence: any s is valid, keep the ray's own
    const math::vec3 s_new = c / len;
    const math::vec3 p_new = glm::cross(r.direction, s_new);
    const math::vec3 s_old = r.s_axis;
    const math::vec3 p_old = p_axis(r);
    const cplx es = r.Es * glm::dot(s_old, s_new) + r.Ep * glm::dot(p_old, s_new);
    const cplx ep = r.Es * glm::dot(s_old, p_new) + r.Ep * glm::dot(p_old, p_new);
    r.s_axis = s_new;
    r.Es     = es;
    r.Ep     = ep;
}

double apply_jones(core::Ray& out, cplx as, cplx ap) noexcept {
    // The interface s axis is perpendicular to the plane of incidence, which contains every
    // outgoing direction, so it stays valid; re-orthogonalise only against rounding.
    math::vec3 s = out.s_axis - glm::dot(out.s_axis, out.direction) * out.direction;
    const double sl = glm::length(s);
    if (sl > 1e-12) out.s_axis = s / sl;

    cplx es = as * out.Es;
    cplx ep = ap * out.Ep;
    const double f = std::norm(es) + std::norm(ep);
    if (f > 0.0) {
        const double k = 1.0 / std::sqrt(f);
        es *= k;
        ep *= k;
    }
    out.Es = es;
    out.Ep = ep;
    return f;
}

double transfer_state(const core::Ray& in, math::vec3 n, core::Ray& out, cplx as,
                      cplx ap) noexcept {
    core::Ray aligned = in;
    align_to_interface(aligned, n);
    out.s_axis    = aligned.s_axis;
    out.Es        = aligned.Es;
    out.Ep        = aligned.Ep;
    out.polarised = true;
    return apply_jones(out, as, ap);
}

void tir_amplitudes(double ci, double n1, double n2, cplx& rs, cplx& rp) noexcept {
    const double si2 = 1.0 - ci * ci;
    const double k   = (n1 / n2) * (n1 / n2) * si2 - 1.0;   // sin^2(theta_t) - 1 >= 0 under TIR
    const cplx   ct(0.0, std::sqrt(k > 0.0 ? k : 0.0));
    rs = (n1 * ci - n2 * ct) / (n1 * ci + n2 * ct);
    rp = (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct);
}

void set_polarisation(core::Ray& r, PolarisationKind kind, double angle_rad) noexcept {
    if (kind == PolarisationKind::Unpolarised) {
        r.polarised = false;
        return;
    }
    const math::vec3 d = r.direction;
    math::vec3 ref{0.0, 0.0, 1.0};
    if (std::fabs(glm::dot(ref, d)) > 0.999) ref = {1.0, 0.0, 0.0};
    const math::vec3 s = glm::normalize(ref - glm::dot(ref, d) * d);
    r.s_axis    = s;
    r.polarised = true;
    const double h = 1.0 / std::sqrt(2.0);
    switch (kind) {
        case PolarisationKind::Linear:
            r.Es = cplx(std::cos(angle_rad), 0.0);
            r.Ep = cplx(std::sin(angle_rad), 0.0);
            break;
        case PolarisationKind::CircularLeft:
            r.Es = cplx(h, 0.0);
            r.Ep = cplx(0.0, h);
            break;
        case PolarisationKind::CircularRight:
            r.Es = cplx(h, 0.0);
            r.Ep = cplx(0.0, -h);
            break;
        case PolarisationKind::Unpolarised:
            break;
    }
}

} // namespace scrt::optics
