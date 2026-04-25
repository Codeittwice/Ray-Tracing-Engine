#pragma once
#include "scrt/materials/Material.hpp"
#include "scrt/optics/Fresnel.hpp"
#include "scrt/optics/Reflect.hpp"
#include "scrt/optics/Refract.hpp"

namespace scrt::materials {

/// Dielectric material (glass, water, etc.); spawns both reflected and refracted rays
/// weighted by exact unpolarised Fresnel coefficients (split mode).
class Dielectric final : public Material {
public:
    /// n: refractive index; absorption_per_m: Beer-Lambert coefficient (1/m).
    explicit Dielectric(double n, double absorption_per_m = 0.0);

    Interaction interact(const core::Ray& r, const core::Hit& h,
                         math::Rng& rng) const override;

    double n()              const { return n_; }
    double absorption()     const { return alpha_; }

    void set_n(double n)             noexcept { n_ = n; }
    void set_absorption(double alpha) noexcept { alpha_ = alpha; }

private:
    double n_;
    double alpha_;
};

} // namespace scrt::materials
