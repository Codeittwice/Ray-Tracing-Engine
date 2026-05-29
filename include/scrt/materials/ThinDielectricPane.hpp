#pragma once
#include "scrt/materials/Material.hpp"
#include "scrt/optics/Fresnel.hpp"
#include "scrt/optics/Reflect.hpp"

namespace scrt::materials {

/// Thin parallel dielectric pane with equivalent slab reflection and transmission losses.
class ThinDielectricPane final : public Material {
public:
    /// Create a pane with refractive index, thickness, and Beer-Lambert absorption.
    ThinDielectricPane(double n, double thickness_m, double absorption_per_m);

    /// Split the ray into slab-reflected and straight-through transmitted components.
    Interaction interact(const core::Ray& r, const core::Hit& h,
                         math::Rng& rng) const override;

private:
    double n_;
    double thickness_m_;
    double absorption_per_m_;
};

} // namespace scrt::materials
