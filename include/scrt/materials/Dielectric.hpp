#pragma once
#include "scrt/materials/Material.hpp"
#include "scrt/optics/Fresnel.hpp"
#include "scrt/optics/Reflect.hpp"
#include "scrt/optics/Refract.hpp"
#include <optional>
#include <string_view>
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
    /// Schott N-SF11 dense flint (Abbe 25.7): the prism glass. Schott's three-term fit.
    static SellmeierCoeffs n_sf11() {
        return {1.73759695, 0.313747346, 1.89878101,
                0.013188707, 0.0623068142, 155.23629};
    }
    /// PMMA (acrylic). Sultanova et al. 2009, a one-term fit; the unused terms are zero with
    /// poles far outside the visible so they cannot divide by zero.
    static SellmeierCoeffs pmma() {
        return {1.1819, 0.0, 0.0,
                0.011313, 1.0e6, 1.0e6};
    }
    /// Polycarbonate. Sultanova et al. 2009, one term, as for PMMA.
    static SellmeierCoeffs polycarbonate() {
        return {1.4182, 0.0, 0.0,
                0.021304, 1.0e6, 1.0e6};
    }

    /// The preset behind a JSON `"sellmeier"` name, or nullopt. The ONE list: the strict
    /// validator and the loader both ask here, so a preset cannot exist in one and not the
    /// other. Water, soda-lime and low-iron glass are deliberately absent: their published
    /// fits have four terms or a Cauchy form, and this struct has three, so they ship as
    /// constant-index dielectrics with the library saying so rather than with invented numbers.
    static std::optional<SellmeierCoeffs> by_name(std::string_view name) {
        if (name == "bk7")           return bk7();
        if (name == "fused_silica")  return fused_silica();
        if (name == "n_sf11")        return n_sf11();
        if (name == "pmma")          return pmma();
        if (name == "polycarbonate") return polycarbonate();
        return std::nullopt;
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
