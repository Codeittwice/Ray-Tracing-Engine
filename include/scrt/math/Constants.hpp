#pragma once

namespace scrt::math {

constexpr double PI                  = 3.14159265358979323846;
constexpr double TWO_PI              = 2.0 * PI;
constexpr double DEG2RAD             = PI / 180.0;
constexpr double RAD2DEG             = 180.0 / PI;
constexpr double SOLAR_HALF_ANGLE_RAD = 4.65e-3;  ///< Sun half-angle (rad)
constexpr double DNI_STANDARD        = 1000.0;     ///< Standard DNI (W/m²)
constexpr double C0                  = 299792458.0; ///< Speed of light (m/s)
constexpr double EPSILON_T           = 1e-6;        ///< Ray self-intersection guard (m)

} // namespace scrt::math
