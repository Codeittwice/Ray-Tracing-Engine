#pragma once
#include "scrt/materials/Material.hpp"

namespace scrt::materials {

/// Ideal Lambertian reflector: scatters into the whole hemisphere, cosine-weighted, keeping an
/// `albedo` fraction of the power. Wave 3 (library section 1.4).
///
/// The first material here that genuinely scatters. RealMirror perturbs the normal, which is a
/// narrow lobe around the specular direction and cannot produce Lambertian scattering however
/// large the slope error is.
///
/// It is also how a partly absorbing surface is modelled (gap G1): `absorber` takes no
/// parameters and absorbs everything, and matte black paint IS a Lambertian surface with a low
/// albedo — around 0.04 — so the realistic absorbing entries in the library are diffusers, not a
/// new absorber type. Note the consequence: such a surface returns a few percent of the light
/// into the scene, where the ideal absorber returned none.
///
/// Sampling: Malley's method (a concentric disk sample lifted onto the hemisphere) gives exactly
/// the cosine-weighted density, whose pdf cancels the cosine in the reflection integral, so an
/// unbiased estimator multiplies the power by the albedo and nothing else. One RNG call per hit.
class Diffuser final : public Material {
public:
    /// albedo in [0, 1]: the fraction of power scattered back. Throws std::invalid_argument
    /// otherwise.
    explicit Diffuser(double albedo);

    Interaction interact(const core::Ray& r, const core::Hit& h, math::Rng& rng) const override;
    /// A Lambertian scatter is random by definition.
    bool deterministic() const override { return false; }

    double albedo() const { return albedo_; }
    /// Throws std::invalid_argument unless the new albedo is in [0, 1].
    void set_albedo(double albedo);

private:
    double albedo_;
};

} // namespace scrt::materials
