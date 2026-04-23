#pragma once
#include "scrt/sources/SunSource.hpp"

namespace scrt::sources {

/// Uniform pillbox sunshape: rays uniformly distributed within a half-angle cone.
class Pillbox final : public SunSource {
public:
    /// half_angle_rad: angular radius of the solar disk (default 4.65 mrad).
    explicit Pillbox(double half_angle_rad = 4.65e-3);

    core::Ray sample_ray(const scene::Aperture& ap, math::Rng& rng) const override;

    double half_angle() const { return half_angle_; }

private:
    double half_angle_;
};

} // namespace scrt::sources
