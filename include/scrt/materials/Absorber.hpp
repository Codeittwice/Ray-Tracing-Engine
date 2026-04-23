#pragma once
#include "scrt/materials/Material.hpp"

namespace scrt::materials {

/// Perfect absorber: deposits all ray power, terminates the path.
class Absorber final : public Material {
public:
    Absorber() = default;

    Interaction interact(const core::Ray& r, const core::Hit& h,
                         math::Rng& rng) const override;
};

} // namespace scrt::materials
