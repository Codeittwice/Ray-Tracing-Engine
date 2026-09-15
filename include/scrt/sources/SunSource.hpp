#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/sources/LightSource.hpp"
#include <stdexcept>
#include <string_view>

namespace scrt::sources {

/// Sun position in horizon coordinates; degrees at the API boundary, radians internally.
///
/// Convention: +Z is the zenith, elevation is measured up from the horizon, and azimuth
/// is a compass bearing starting at +Y (North) and increasing toward +X (East). This
/// matches the box-receiver face naming in SceneLoader, which places `north_wall` at +Y
/// and `east_wall` at +X.
struct SunAngles {
    double azimuth_deg   {180.0};  ///< Compass bearing from +Y (North) toward +X (East).
    double elevation_deg {90.0};   ///< Degrees above the horizon; 90 is the zenith.
};

/// Abstract sun model; samples primary rays through its own collection aperture.
///
/// The aperture lives HERE, not on the scene: it is solar sampling apparatus (the disk the
/// sun's rays are launched from), not a property of the world. A laser has no aperture and
/// must not be made to carry one.
class SunSource : public LightSource {
public:
    /// DNI * aperture area * foreshortening, in the same left-to-right association the tracer
    /// used before the aperture moved here, so the per-ray power is the same double.
    double total_power_w() const override {
        return dni_ * aperture_.area() * aperture_.cosine_to(to_sun());
    }

    std::string_view type_name() const override { return "sun"; }
    const SunSource* as_sun() const override { return this; }
          SunSource* as_sun()       override { return this; }

    /// The disk primary rays are launched from; must cover the scene's shadow toward the sun.
    const scene::Aperture& aperture() const { return aperture_; }
    /// Replace the collection aperture. Resolve auto_fit before calling; this stores verbatim.
    void set_aperture(const scene::Aperture& ap) { aperture_ = ap; }

    /// Wavelength stamped on every sampled ray [nm].
    ///
    /// The engine is strictly monochromatic, one wavelength per ray, with no spectral weighting
    /// anywhere. 550 nm is a CONVENTION (the photopic peak), not a derivation from the solar
    /// spectrum, and it equals core::Ray's own default so the stamp writes the double that was
    /// already there. Dielectric::n_at reads it for Sellmeier dispersion.
    double wavelength_nm() const { return wavelength_nm_; }

    /// Horizontal component below which azimuth is treated as degenerate (at the pole).
    ///
    /// cos(elevation) for elevation == 90 degrees evaluates to ~6.1e-17 in double, so the
    /// threshold must sit well above that to recognise the zenith reliably; 1e-12 rad is
    /// roughly four orders of magnitude larger, yet ~2e-7 arcsec — nine orders of
    /// magnitude finer than the sun's own 4.65e-3 rad angular radius, so no physically
    /// meaningful sun position is ever swallowed by it.
    static constexpr double POLE_HORIZONTAL_EPS = 1e-12;

    /// Propagation direction (away from the sun) for the given horizon angles.
    ///
    /// Deliberately total: it maps a below-horizon elevation to the upward-travelling
    /// direction that elevation really names, so angles_from_direction can round-trip it.
    /// Use below_horizon() to decide whether such a sun should be simulated at all.
    static math::vec3 direction_from_angles(SunAngles a);

    /// Horizon angles for a propagation direction; returns fallback azimuth at the pole.
    ///
    /// Throws std::invalid_argument on a zero-length direction, which names no sun at all;
    /// the previous glm::normalize path returned a silent NaN pair instead.
    static SunAngles angles_from_direction(math::vec3 propagation_dir,
                                           double fallback_azimuth_deg = 180.0);

    /// True when these angles put the sun under the horizon (elevation below 0 degrees).
    static bool below_horizon(SunAngles a) { return a.elevation_deg < 0.0; }

    /// True when this propagation direction carries light upward, i.e. from below ground.
    ///
    /// +Z is the zenith, and sun_direction_ is where the light travels, so a strictly
    /// positive z component is exactly a negative solar elevation. Exact comparison, not
    /// an epsilon: elevation 0 yields z == +0.0, which is not > 0.
    static bool below_horizon(math::vec3 propagation_dir) { return propagation_dir.z > 0.0; }

    /// True when this source's sun currently sits under the horizon (yields no daylight).
    bool sun_below_horizon() const { return below_horizon(sun_direction_); }

    /// Point the sun by propagation direction; throws std::invalid_argument on a zero vector.
    void set_sun_direction(math::vec3 d) {
        if (glm::dot(d, d) <= 0.0)
            throw std::invalid_argument(
                "SunSource::set_sun_direction: zero-length direction names no sun");
        sun_direction_ = math::safe_normalize(d);
    }

    /// Unit vector along which sunlight travels (from the sun toward the scene).
    math::vec3 sun_direction() const { return sun_direction_; }

    /// Unit vector from the scene toward the sun (opposite the propagation direction).
    math::vec3 to_sun() const { return -sun_direction_; }

    /// Set the sun position from horizon angles.
    ///
    /// Accepts a below-horizon elevation rather than throwing, because a scene may be
    /// mid-edit and because the geometry is still well defined; callers that will act on
    /// the result (trace, auto-fit an aperture, draw a sun widget) must check
    /// sun_below_horizon() and tell the user. scene::Aperture::auto_fit rejects such a sun
    /// outright, since fitting to it silently buries the aperture under the cooker.
    void set_sun_angles(SunAngles a) { sun_direction_ = direction_from_angles(a); }

    /// Current sun position in horizon angles.
    SunAngles sun_angles(double fallback_azimuth_deg = 180.0) const {
        return angles_from_direction(sun_direction_, fallback_azimuth_deg);
    }

    /// Set the direct normal irradiance carried by the beam [W/m^2].
    void set_dni(double dni_wm2) { dni_ = dni_wm2; }

    /// Direct normal irradiance carried by the beam [W/m^2].
    double dni() const { return dni_; }

protected:
    SunSource() = default;
    math::vec3      sun_direction_ {0.0, 0.0, -1.0};
    double          dni_           {1000.0};
    scene::Aperture aperture_      {};
    double          wavelength_nm_ {550.0};
};

} // namespace scrt::sources
