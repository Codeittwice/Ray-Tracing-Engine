#include <doctest/doctest.h>
#include "scrt/optics/Fresnel.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>

using namespace scrt::optics;

// T2 -----------------------------------------------------------------------

TEST_CASE("T2: Fresnel R = 0.04 at normal incidence, air-glass") {
    // At normal incidence cos_i = cos_t = 1; R = ((n1-n2)/(n1+n2))^2 = 0.04.
    auto res = fresnel_unpolarized(1.0, 1.0, 1.0, 1.5);
    CHECK(res.R == doctest::Approx(0.04).epsilon(1e-9));
    CHECK(res.T == doctest::Approx(0.96).epsilon(1e-9));
}

TEST_CASE("Fresnel R + T = 1 for all angles, air-glass") {
    for (int deg = 0; deg <= 89; ++deg) {
        double theta_i = deg * scrt::math::DEG2RAD;
        double n1 = 1.0, n2 = 1.5;
        double cos_i = std::cos(theta_i);
        double sin_t = (n1 / n2) * std::sin(theta_i);
        if (sin_t >= 1.0) break;
        double cos_t = std::sqrt(1.0 - sin_t * sin_t);
        auto res = fresnel_unpolarized(cos_i, cos_t, n1, n2);
        CHECK(res.R + res.T == doctest::Approx(1.0).epsilon(1e-12));
    }
}

TEST_CASE("Schlick R0 = 0.04 for air-glass") {
    CHECK(schlick_R0(1.0, 1.5) == doctest::Approx(0.04).epsilon(1e-9));
}

TEST_CASE("Schlick approximation at normal incidence equals R0") {
    double R0 = schlick_R0(1.0, 1.5);
    CHECK(schlick_R(1.0, R0) == doctest::Approx(R0).epsilon(1e-15));
}

TEST_CASE("Schlick approaches 1 at grazing incidence") {
    double R0 = schlick_R0(1.0, 1.5);
    CHECK(schlick_R(0.0, R0) == doctest::Approx(1.0).epsilon(1e-15));
}
