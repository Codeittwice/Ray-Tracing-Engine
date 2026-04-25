#pragma once
#include "scrt/materials/Material.hpp"
#include "scrt/optics/Reflect.hpp"

namespace scrt::materials {

/// Imperfect mirror with constant spectral reflectance and optional Gaussian slope error.
class RealMirror final : public Material {
public:
    /// reflectance: fraction in [0,1]; slope_error_mrad: 1-sigma Gaussian normal perturbation.
    explicit RealMirror(double reflectance, double slope_error_mrad = 0.0);

    Interaction interact(const core::Ray& r, const core::Hit& h,
                         math::Rng& rng) const override;

    double reflectance()    const { return rho_; }
    double slope_error()    const { return slope_error_; }

    void set_reflectance(double r)       noexcept { rho_ = r; }
    void set_slope_error_mrad(double s)  noexcept { slope_error_ = s; }

private:
    double rho_;
    double slope_error_;  ///< 1-sigma in mrad.
};

} // namespace scrt::materials
