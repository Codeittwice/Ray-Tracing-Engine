#include "scrt/materials/PolarisingOptics.hpp"

#include "scrt/math/Constants.hpp"
#include "scrt/optics/Polarisation.hpp"
#include "scrt/optics/Reflect.hpp"
#include "scrt/surfaces/Surface.hpp"

#include <cmath>
#include <complex>
#include <stdexcept>
#include <string>

namespace scrt::materials {

namespace {

using optics::cplx;

void require_er(double er, const char* who) {
    if (!std::isfinite(er) || er < 1.0)
        throw std::invalid_argument(std::string(who) + ": extinction_ratio must be finite and >= 1 (got " +
                                    std::to_string(er) + ")");
}

void require_unit(double v, const char* who, const char* what) {
    if (!std::isfinite(v) || v < 0.0 || v > 1.0)
        throw std::invalid_argument(std::string(who) + ": " + what + " must be in [0, 1] (got " +
                                    std::to_string(v) + ")");
}

/// The part's in-plane axis at `deg` from its local +X, in world space and made perpendicular to
/// the ray. Falls back to the ray's own s axis if the axis runs along the ray (a degenerate pose).
math::vec3 axis_across_ray(const core::Hit& h, double deg, const core::Ray& r) {
    const double a = deg * math::PI / 180.0;
    math::vec3   local{std::cos(a), std::sin(a), 0.0};
    math::vec3   w = h.surface ? h.surface->transform().direction_to_world(local) : local;
    w -= glm::dot(w, r.direction) * r.direction;
    const double len = glm::length(w);
    return len > 1e-9 ? w / len : r.s_axis;
}

/// Complex component of a polarised ray's field along a real unit vector.
cplx component(const core::Ray& r, math::vec3 u) {
    const auto f = optics::world_field(r);
    return f.x * u.x + f.y * u.y + f.z * u.z;
}

/// The outgoing ray of a zero-thickness transmissive element: same direction, new origin.
core::Ray pass_through(const core::Ray& r, const core::Hit& h) {
    core::Ray out = r;
    out.origin    = h.position;
    out.bounces   = r.bounces + 1;
    return out;
}

} // namespace

// ---- Polariser ----------------------------------------------------------------

Polariser::Polariser(double axis_deg, double er, double k1) : axis_deg_(0.0), extinction_(1.0), k1_(1.0) {
    set_axis_deg(axis_deg);
    set_extinction_ratio(er);
    set_transmission(k1);
}

void Polariser::set_axis_deg(double deg) {
    if (!std::isfinite(deg)) throw std::invalid_argument("Polariser: transmission_axis_deg must be finite");
    axis_deg_ = deg;
}
void Polariser::set_extinction_ratio(double er) { require_er(er, "Polariser"); extinction_ = er; }
void Polariser::set_transmission(double k1) { require_unit(k1, "Polariser", "transmission"); k1_ = k1; }

Interaction Polariser::interact(const core::Ray& r, const core::Hit& h, math::Rng& /*rng*/) const {
    const math::vec3 t = axis_across_ray(h, axis_deg_, r);
    Interaction      ia;
    ia.kind        = InteractionKind::Refracted;
    ia.transmitted = pass_through(r, h);
    core::Ray& out = ia.transmitted;

    if (!r.polarised) {
        out.power     = r.power * 0.5 * k1_ * (1.0 + 1.0 / extinction_);
        out.polarised = true;
        out.s_axis    = t;
        out.Es        = cplx(1.0, 0.0);
        out.Ep        = cplx(0.0, 0.0);
        return ia;
    }
    // Along the axis (s) and across it (p = direction x s); then k1 and k1/ER as power factors.
    const math::vec3 b = glm::cross(r.direction, t);
    out.s_axis = t;
    out.Es     = component(r, t);
    out.Ep     = component(r, b);
    out.power  = r.power * optics::apply_jones(out, std::sqrt(k1_), std::sqrt(k1_ / extinction_));
    return ia;
}

// ---- Waveplate ----------------------------------------------------------------

Waveplate::Waveplate(double retardance, double axis_deg, double k) : retardance_(0.0), axis_deg_(0.0), k_(1.0) {
    set_retardance_waves(retardance);
    set_fast_axis_deg(axis_deg);
    set_transmission(k);
}

void Waveplate::set_retardance_waves(double w) {
    if (!std::isfinite(w)) throw std::invalid_argument("Waveplate: retardance_waves must be finite");
    retardance_ = w;
}
void Waveplate::set_fast_axis_deg(double deg) {
    if (!std::isfinite(deg)) throw std::invalid_argument("Waveplate: fast_axis_deg must be finite");
    axis_deg_ = deg;
}
void Waveplate::set_transmission(double k) { require_unit(k, "Waveplate", "transmission"); k_ = k; }

Interaction Waveplate::interact(const core::Ray& r, const core::Hit& h, math::Rng& /*rng*/) const {
    Interaction ia;
    ia.kind        = InteractionKind::Refracted;
    ia.transmitted = pass_through(r, h);
    core::Ray& out = ia.transmitted;
    out.power      = r.power * k_;
    if (!r.polarised) return ia;   // a retarder does not polarise

    // Fast axis as s, slow axis as p; the slow component lags by 2 pi Gamma. With the e^{-i w t}
    // time convention of optics::set_polarisation a lag is a factor e^{+i phi}.
    const math::vec3 f = axis_across_ray(h, axis_deg_, r);
    const math::vec3 s = glm::cross(r.direction, f);
    out.s_axis = f;
    out.Es     = component(r, f);
    out.Ep     = component(r, s) * std::exp(cplx(0.0, 2.0 * math::PI * retardance_));
    optics::apply_jones(out, 1.0, 1.0);   // renormalise only; power was set above
    return ia;
}

// ---- PolarisingBeamSplitter ---------------------------------------------------

PolarisingBeamSplitter::PolarisingBeamSplitter(double er) : extinction_(1.0) { set_extinction_ratio(er); }

void PolarisingBeamSplitter::set_extinction_ratio(double er) {
    require_er(er, "PolarisingBeamSplitter");
    extinction_ = er;
}

Interaction PolarisingBeamSplitter::interact(const core::Ray& r, const core::Hit& h,
                                             math::Rng& /*rng*/) const {
    const double leak = 1.0 / extinction_;
    Interaction  ia;
    ia.kind = InteractionKind::Split;

    ia.reflected           = r;
    ia.reflected.origin    = h.position;
    ia.reflected.direction = optics::reflect(r.direction, h.normal);
    ia.reflected.bounces   = r.bounces + 1;
    ia.transmitted         = pass_through(r, h);

    if (!r.polarised) {
        // 50:50 by construction: (Rs + Rp) / 2 = (1 - leak + leak) / 2. Each branch leaves as the
        // pure state it mostly is; the leak's opposite state is folded in (see the class note).
        ia.reflected.power   = 0.5 * r.power;
        ia.transmitted.power = 0.5 * r.power;
        core::Ray s_state = r;
        s_state.polarised = true;
        s_state.Es        = cplx(1.0, 0.0);
        s_state.Ep        = cplx(0.0, 0.0);
        math::vec3 c      = glm::cross(r.direction, h.normal);
        if (glm::length(c) > 1e-9) s_state.s_axis = glm::normalize(c);
        else {
            math::vec3 ref = std::fabs(r.direction.z) < 0.9 ? math::vec3{0, 0, 1} : math::vec3{1, 0, 0};
            s_state.s_axis = glm::normalize(ref - glm::dot(ref, r.direction) * r.direction);
        }
        optics::transfer_state(s_state, h.normal, ia.reflected, -1.0, 1.0);
        core::Ray p_state = s_state;
        p_state.Es        = cplx(0.0, 0.0);
        p_state.Ep        = cplx(1.0, 0.0);
        optics::transfer_state(p_state, h.normal, ia.transmitted, 1.0, 1.0);
        return ia;
    }
    ia.reflected.power = r.power * optics::transfer_state(r, h.normal, ia.reflected,
                                                          -std::sqrt(1.0 - leak), std::sqrt(leak));
    ia.transmitted.power = r.power * optics::transfer_state(r, h.normal, ia.transmitted,
                                                            std::sqrt(leak), std::sqrt(1.0 - leak));
    return ia;
}

} // namespace scrt::materials
