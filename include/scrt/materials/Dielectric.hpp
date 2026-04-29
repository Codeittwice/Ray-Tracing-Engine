#pragma once
#include "scrt/materials/Material.hpp"
#include "scrt/optics/Fresnel.hpp"
#include "scrt/optics/Reflect.hpp"
#include "scrt/optics/Refract.hpp"
#include <optional>
#include <utility>
#include <vector>

namespace scrt::materials {

/// Sellmeier coefficients for wavelength-dependent refractive index.
/// n²(λ) = 1 + B1·λ²/(λ²−C1) + B2·λ²/(λ²−C2) + B3·λ²/(λ²−C3), λ in µm.
struct SellmeierCoeffs {
    double B1, B2, B3;
    double C1, C2, C3;  ///< Pole wavelengths squared (µm²).

    /// Standard N-BK7 borosilicate glass coefficients.
    static SellmeierCoeffs bk7() {
        return {1.03961212, 0.231792344, 1.01046945,
                0.00600069867, 0.0200179144, 103.560653};
    }
    /// Fused silica (SiO₂) coefficients.
    static SellmeierCoeffs fused_silica() {
        return {0.6961663, 0.4079426, 0.8974794,
                0.0046791, 0.0135121, 97.934003};
    }
};

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

    void set_n(double n)              noexcept { n_ = n; }
    void set_absorption(double alpha)  noexcept { alpha_ = alpha; }

    /// Enable wavelength-dependent index via the Sellmeier equation.
    void set_sellmeier(SellmeierCoeffs c) { sellmeier_ = c; }
    bool has_sellmeier()    const { return sellmeier_.has_value(); }

    /// Refractive index at the given wavelength (nm).  Falls back to n_ if no Sellmeier set.
    double n_at(double wavelength_nm) const;

    /// Replace the scalar absorption coefficient with a piecewise-linear spectrum.
    /// @param spec  Pairs of (wavelength_nm, alpha_per_m), any order; sorted internally.
    void set_alpha_spectrum(std::vector<std::pair<double,double>> spec);
    bool has_alpha_spectrum() const { return !alpha_spectrum_.empty(); }

    /// Beer-Lambert coefficient (m⁻¹) at the given wavelength.
    /// Linearly interpolates the spectrum if set; otherwise returns the scalar alpha.
    double alpha_at(double wavelength_nm) const;

private:
    double n_;
    double alpha_;
    std::optional<SellmeierCoeffs>          sellmeier_;
    std::vector<std::pair<double,double>>   alpha_spectrum_;  ///< Sorted by wavelength_nm.
};

} // namespace scrt::materials
