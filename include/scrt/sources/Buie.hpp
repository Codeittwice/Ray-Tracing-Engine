#pragma once
#include "scrt/math/Sampling.hpp"
#include "scrt/sources/SunSource.hpp"

namespace scrt::sources {

/// Buie et al. 2003 solar radiance profile parameterised by the circumsolar ratio (CSR).
/// Produces a more realistic sun disk + aureole than the flat Pillbox model.
class Buie final : public SunSource {
public:
    /// chi: circumsolar ratio χ ∈ [0.02, 0.10].  Typical clear day: 0.05.
    explicit Buie(double chi = 0.05);

    core::Ray sample_ray(const scene::Aperture& ap, math::Rng& rng) const override;

    /// Returns the circumsolar ratio used to construct this distribution.
    double chi() const { return chi_; }

private:
    double              chi_;
    math::TabulatedDist1D theta_sampler_;  ///< Inverse-CDF sampler for angular radius θ.
};

} // namespace scrt::sources
