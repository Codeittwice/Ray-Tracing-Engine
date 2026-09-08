#pragma once
#include "scrt/core/Ray.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"

namespace scrt::scene { struct Aperture; }

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

/// Abstract sun model; samples primary rays through the collection aperture.
class SunSource {
public:
    virtual ~SunSource() = default;
    SunSource(const SunSource&) = delete;
    SunSource& operator=(const SunSource&) = delete;
    SunSource(SunSource&&) = delete;
    SunSource& operator=(SunSource&&) = delete;

    /// Draw one primary ray from the aperture toward the sun.
    virtual core::Ray sample_ray(const scene::Aperture& ap, math::Rng& rng) const = 0;

    /// Horizontal component below which azimuth is treated as degenerate (at the pole).
    ///
    /// cos(elevation) for elevation == 90 degrees evaluates to ~6.1e-17 in double, so the
    /// threshold must sit well above that to recognise the zenith reliably; 1e-12 rad is
    /// roughly four orders of magnitude larger, yet ~2e-7 arcsec — nine orders of
    /// magnitude finer than the sun's own 4.65e-3 rad angular radius, so no physically
    /// meaningful sun position is ever swallowed by it.
    static constexpr double POLE_HORIZONTAL_EPS = 1e-12;

    /// Propagation direction (away from the sun) for the given horizon angles.
    static math::vec3 direction_from_angles(SunAngles a);

    /// Horizon angles for a propagation direction; returns fallback azimuth at the pole.
    static SunAngles angles_from_direction(math::vec3 propagation_dir,
                                           double fallback_azimuth_deg = 180.0);

    void set_sun_direction(math::vec3 d) { sun_direction_ = glm::normalize(d); }
    math::vec3 sun_direction() const { return sun_direction_; }

    /// Unit vector from the scene toward the sun (opposite the propagation direction).
    math::vec3 to_sun() const { return -sun_direction_; }

    /// Set the sun position from horizon angles.
    void set_sun_angles(SunAngles a) { sun_direction_ = direction_from_angles(a); }

    /// Current sun position in horizon angles.
    SunAngles sun_angles(double fallback_azimuth_deg = 180.0) const {
        return angles_from_direction(sun_direction_, fallback_azimuth_deg);
    }

    void set_dni(double dni_wm2) { dni_ = dni_wm2; }
    double dni() const { return dni_; }

protected:
    SunSource() = default;
    math::vec3 sun_direction_ {0.0, 0.0, -1.0};
    double     dni_           {1000.0};
};

} // namespace scrt::sources
