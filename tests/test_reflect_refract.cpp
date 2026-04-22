#include <doctest/doctest.h>
#include "scrt/optics/Reflect.hpp"
#include "scrt/optics/Refract.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

using namespace scrt::math;
using namespace scrt::optics;

// T4 -----------------------------------------------------------------------

TEST_CASE("T4: reflected ray has unit length") {
    vec3 n{0, 1, 0};
    vec3 i = safe_normalize(vec3{1, -1, 0});
    CHECK(glm::length(reflect(i, n)) == doctest::Approx(1.0).epsilon(1e-14));
}

TEST_CASE("T4: double-reflect recovers original direction") {
    vec3 n{0, 1, 0};
    vec3 i = safe_normalize(vec3{1, -2, 0});
    vec3 r2 = reflect(reflect(i, n), n);
    CHECK(r2.x == doctest::Approx(i.x).epsilon(1e-14));
    CHECK(r2.y == doctest::Approx(i.y).epsilon(1e-14));
    CHECK(r2.z == doctest::Approx(i.z).epsilon(1e-14));
}

TEST_CASE("T4: angle of reflection equals angle of incidence") {
    vec3 n{0, 1, 0};
    vec3 i = safe_normalize(vec3{2, -3, 1});
    vec3 r = reflect(i, n);
    double cos_i = std::abs(glm::dot(i, n));
    double cos_r = std::abs(glm::dot(r, n));
    CHECK(cos_i == doctest::Approx(cos_r).epsilon(1e-14));
}

// T1 -----------------------------------------------------------------------

TEST_CASE("T1: Snell's law, air->glass at 30 degrees") {
    // i points toward surface (downward), n points away from surface (upward).
    double theta_i = 30.0 * DEG2RAD;
    vec3 n{0, 1, 0};
    vec3 i{std::sin(theta_i), -std::cos(theta_i), 0.0};
    bool tir = false;
    vec3 t = refract(i, n, 1.0 / 1.5, tir);
    CHECK(!tir);
    // sin(theta_t) = sin(theta_i) / n2
    double sin_t    = std::sqrt(t.x * t.x + t.z * t.z);
    double expected = std::sin(theta_i) / 1.5;
    CHECK(sin_t == doctest::Approx(expected).epsilon(1e-9));
    CHECK(t.y < 0.0);  // transmitted ray continues downward
}

// T3 -----------------------------------------------------------------------

TEST_CASE("T3: TIR glass->air at 45 degrees") {
    // Critical angle for n=1.5 is ~41.8 deg; 45 > 41.8 => TIR.
    double theta_i = 45.0 * DEG2RAD;
    vec3 n{0, 1, 0};
    vec3 i{std::sin(theta_i), -std::cos(theta_i), 0.0};
    bool tir = false;
    vec3 t = refract(i, n, 1.5 / 1.0, tir);
    CHECK(tir);
    vec3 r = reflect(i, n);
    CHECK(t.x == doctest::Approx(r.x).epsilon(1e-15));
    CHECK(t.y == doctest::Approx(r.y).epsilon(1e-15));
    CHECK(t.z == doctest::Approx(r.z).epsilon(1e-15));
}

TEST_CASE("refract below critical angle is not TIR") {
    // 30 deg < 41.8 deg critical angle for glass->air.
    double theta_i = 30.0 * DEG2RAD;
    vec3 n{0, 1, 0};
    vec3 i{std::sin(theta_i), -std::cos(theta_i), 0.0};
    bool tir = false;
    refract(i, n, 1.5 / 1.0, tir);
    CHECK(!tir);
}
