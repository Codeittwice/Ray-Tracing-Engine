#include "scrt/sources/SunSource.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

namespace scrt::sources {

math::vec3 SunSource::direction_from_angles(SunAngles a) {
    const double el = a.elevation_deg * math::DEG2RAD;
    const double az = a.azimuth_deg   * math::DEG2RAD;

    double       cos_el = std::cos(el);
    const double sin_el = std::sin(el);

    // At the pole the horizontal component is pure round-off (cos(pi/2) == 6.1e-17 in
    // double, not 0). Snap it away so a zenith sun yields exactly (0, 0, -1) for every
    // azimuth, which is what the existing scenes and their golden fluxes assume.
    if (std::abs(cos_el) < POLE_HORIZONTAL_EPS)
        cos_el = 0.0;

    // Azimuth is a bearing from +Y (North) toward +X (East): x = sin(az), y = cos(az).
    const math::vec3 to_sun{cos_el * std::sin(az), cos_el * std::cos(az), sin_el};
    return -glm::normalize(to_sun);
}

SunAngles SunSource::angles_from_direction(math::vec3 propagation_dir,
                                           double fallback_azimuth_deg) {
    // sun_direction_ is where light travels; the sun itself sits the other way.
    const math::vec3 to_sun = -glm::normalize(propagation_dir);
    const double     horiz  = std::hypot(to_sun.x, to_sun.y);

    SunAngles a;
    a.elevation_deg = std::atan2(to_sun.z, horiz) * math::RAD2DEG;

    if (horiz < POLE_HORIZONTAL_EPS) {
        // Straight overhead (or underfoot): every azimuth names the same direction, so
        // atan2 would return an arbitrary value. Hand back the caller's choice verbatim.
        a.azimuth_deg = fallback_azimuth_deg;
        return a;
    }

    double az = std::atan2(to_sun.x, to_sun.y) * math::RAD2DEG;
    if (az < 0.0)
        az += 360.0;  // atan2 spans (-180, 180]; report a bearing in [0, 360).
    a.azimuth_deg = az;
    return a;
}

} // namespace scrt::sources
