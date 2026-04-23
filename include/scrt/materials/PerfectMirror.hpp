#pragma once
#include "scrt/materials/Material.hpp"
#include "scrt/optics/Reflect.hpp"

namespace scrt::materials {

/// Ideal mirror: reflectance = 1, reflected power equals incident power.
class PerfectMirror final : public Material {
public:
    PerfectMirror() = default;

    Interaction interact(const core::Ray& r, const core::Hit& h,
                         math::Rng& rng) const override;
};

} // namespace scrt::materials
