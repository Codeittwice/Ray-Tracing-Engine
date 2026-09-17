#pragma once
#include "scrt/optics/Polarisation.hpp"
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

    /// Polarisation stamped on every ray. Unpolarised (the default) leaves rays on the averaged,
    /// pre-Wave-5 optics; linear takes an angle in degrees from the vertical reference (see
    /// optics::set_polarisation). No RNG draws either way, so seeding is unchanged.
    void set_polarisation(optics::PolarisationKind kind, double linear_deg = 0.0);
    optics::PolarisationKind polarisation() const { return polarisation_; }
    double polarisation_linear_deg() const { return polarisation_deg_; }

    /// How rays are placed over the beam. Random (the default) is Monte Carlo sampling, which every
    /// existing scene uses. Grid is a deterministic sunflower (Vogel) spiral over the beam disk and
    /// a low-discrepancy pattern over the divergence cone, one point per allotted ray: required by a
    /// coherent receiver, because random rays summed with their phases give speckle, not fringes.
    enum class Sampling { Random, Grid };
    void     set_sampling(Sampling s) { sampling_ = s; }
    Sampling sampling() const { return sampling_; }

    /// Coherence length [m]; 0 (the default) means fully coherent. Throws unless finite and >= 0.
    void   set_coherence_length_m(double m);
    double coherence_length_m() const override { return coherence_length_m_; }

    bool      deterministic_sampling() const override { return sampling_ == Sampling::Grid; }
    core::Ray sample_ray_indexed(math::Rng& rng, std::size_t k, std::size_t n) const override;

private:
    /// Builds the ray from a disk point in [-1,1]^2 (unit disk) and cap/azimuth fractions in [0,1).
    core::Ray make_ray(math::vec2 disk, double cap_u, double phi_u) const;

    Sampling   sampling_           {Sampling::Random};
    double     coherence_length_m_ {0.0};
    math::vec3 origin_          {0.0, 0.0, 1.0};
    math::vec3 direction_       {0.0, 0.0, -1.0};
    double     power_w_         {1.0};
    double     wavelength_nm_   {632.8};   ///< He-Ne red, the bench default.
    double     beam_diameter_m_ {0.001};
    double     divergence_mrad_ {0.0};
    optics::PolarisationKind polarisation_ {optics::PolarisationKind::Unpolarised};
    double     polarisation_deg_ {0.0};
};

} // namespace scrt::sources
