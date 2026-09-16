#include "scrt/materials/Diffuser.hpp"
#include "scrt/scene/Aperture.hpp"   // orthonormal_frame
#include <cmath>
#include <stdexcept>
#include <string>

namespace scrt::materials {

namespace {
void check(double a) {
    if (!std::isfinite(a) || a < 0.0 || a > 1.0)
        throw std::invalid_argument("Diffuser: albedo must be finite and within [0, 1] (got " +
                                    std::to_string(a) + ")");
}
} // namespace

Diffuser::Diffuser(double albedo) : albedo_(albedo) { check(albedo_); }

void Diffuser::set_albedo(double albedo) {
    check(albedo);
    albedo_ = albedo;
}

Interaction Diffuser::interact(const core::Ray& r, const core::Hit& h, math::Rng& rng) const {
    // h.normal already opposes the incoming ray, so the outgoing hemisphere is about it.
    math::vec3 u, v;
    scene::orthonormal_frame(h.normal, u, v);

    // Malley: a point sampled uniformly on the unit disk, lifted to the hemisphere, is
    // distributed as cos(theta)/pi. Its pdf cancels the cosine factor in the reflection
    // integral exactly, so the surviving power is albedo * incoming and nothing more.
    const math::vec2 disk = rng.unit_disk_concentric();
    const double     z2   = 1.0 - disk.x * disk.x - disk.y * disk.y;
    const double     z    = std::sqrt(z2 > 0.0 ? z2 : 0.0);

    Interaction ia;
    ia.kind                = InteractionKind::Reflected;
    ia.reflected           = r;
    ia.reflected.origin    = h.position;
    ia.reflected.direction = math::safe_normalize(disk.x * u + disk.y * v + z * h.normal);
    ia.reflected.power     = r.power * albedo_;
    ia.reflected.bounces   = r.bounces + 1;
    // A Lambertian scatterer depolarises. For an unpolarised ray this assigns what is already there.
    ia.reflected.polarised = false;
    return ia;
}

} // namespace scrt::materials
