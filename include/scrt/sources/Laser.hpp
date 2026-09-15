#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/sources/LightSource.hpp"
#include <string_view>

namespace scrt::sources {

/// A laser: a top-hat beam of authored power leaving a small disk along one direction.
///
/// The first source that is not a sun, and the reason the source layer exists: it has watts,
/// not an irradiance, and no aperture. Rays start uniformly over a disk of `beam_diameter_m`
/// centred on `origin` and perpendicular to `direction`, and leave within a cone of half-angle
/// `divergence_mrad / 2` (the FULL-angle divergence, as laser datasheets quote it), exactly
/// uniformly over the spherical cap, so the far field is a top hat rather than a Gaussian. A Gaussian profile is
/// a later refinement, not a correction: the power model here is exact either way.
class Laser final : public LightSource {
public:
    Laser() = default;

    double           total_power_w() const override { return power_w_; }
    core::Ray        sample_ray(math::Rng& rng) const override;
    std::string_view type_name() const override { return "laser"; }

    /// Centre of the emitting disk [m].
    void set_origin(math::vec3 o) { origin_ = o; }
    math::vec3 origin() const { return origin_; }

    /// Beam axis; throws std::invalid_argument on a zero vector, which names no beam.
    void set_direction(math::vec3 d);
    /// Unit beam axis.
    math::vec3 direction() const { return direction_; }

    /// Optical power [W]; throws on a negative or non-finite value. 0 is a laser switched off.
    void set_power_w(double w);
    double power_w() const { return power_w_; }

    /// Wavelength stamped on every ray [nm]; throws unless finite and > 0.
    void set_wavelength_nm(double nm);
    double wavelength_nm() const { return wavelength_nm_; }

    /// Emitting disk diameter [m]; throws on negative or non-finite. 0 is a pencil beam.
    void set_beam_diameter_m(double d);
    double beam_diameter_m() const { return beam_diameter_m_; }

    /// Full-angle divergence [mrad]; throws unless finite and in [0, 1000*pi] (180 degrees).
    /// 0 is collimated.
    void set_divergence_mrad(double mrad);
    double divergence_mrad() const { return divergence_mrad_; }

private:
    math::vec3 origin_          {0.0, 0.0, 1.0};
    math::vec3 direction_       {0.0, 0.0, -1.0};
    double     power_w_         {1.0};
    double     wavelength_nm_   {632.8};   ///< He-Ne red, the bench default.
    double     beam_diameter_m_ {0.001};
    double     divergence_mrad_ {0.0};
};

} // namespace scrt::sources
