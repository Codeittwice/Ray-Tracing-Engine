#include <doctest/doctest.h>
#include "scrt/optics/Fresnel.hpp"
#include "scrt/math/Constants.hpp"
#include <cmath>
#include <initializer_list>

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

// Wave 5 Stage 1. fresnel_unpolarized now reads rs and rp from fresnel_amplitudes. The reference
// below is the pre-Wave-5 body VERBATIM; equality is bitwise (==), because a last-bit change here
// moves every dielectric scene in the corpus while an Approx check would still pass.
namespace {
double legacy_unpolarized_R(double ci, double ct, double n1, double n2) {
    double rs = (n1 * ci - n2 * ct) / (n1 * ci + n2 * ct);
    double rp = (n2 * ci - n1 * ct) / (n2 * ci + n1 * ct);
    double R  = 0.5 * (rs * rs + rp * rp);
    return R;
}
} // namespace

TEST_CASE("Fresnel: the unpolarized result is bit-identical to the pre-amplitude formula") {
    const double pairs[][2] = {{1.0, 1.5}, {1.5, 1.0}, {1.0, 1.7847}, {1.33, 1.0},
                               {1.0, 1.4917}, {1.5168, 1.0}, {1.0, 2.4}};
    int checked = 0;
    for (const auto& pr : pairs) {
        const double n1 = pr[0], n2 = pr[1];
        for (int k = 0; k <= 900; ++k) {
            const double theta = (k / 1000.0) * (scrt::math::PI / 2.0);
            const double ci    = std::cos(theta);
            const double sin_t = (n1 / n2) * std::sin(theta);
            if (sin_t >= 1.0) continue;                     // TIR: no Fresnel split
            const double ct = std::sqrt(1.0 - sin_t * sin_t);
            const auto   fr = fresnel_unpolarized(ci, ct, n1, n2);
            const double R0 = legacy_unpolarized_R(ci, ct, n1, n2);
            CHECK(fr.R == R0);
            CHECK(fr.T == 1.0 - R0);
            ++checked;
        }
    }
    CHECK(checked > 5000);
}

TEST_CASE("Fresnel amplitudes: s and p each conserve energy, and agree at normal incidence") {
    for (double n2 : {1.33, 1.5, 1.7847}) {
        const double n1 = 1.0;
        for (int k = 0; k < 89; ++k) {
            const double theta = k * scrt::math::PI / 180.0;
            const double ci    = std::cos(theta);
            const double st    = (n1 / n2) * std::sin(theta);
            const double ct    = std::sqrt(1.0 - st * st);
            const auto   a     = fresnel_amplitudes(ci, ct, n1, n2);
            const double k_t   = (n2 * ct) / (n1 * ci);
            CHECK(a.rs * a.rs + k_t * a.ts * a.ts == doctest::Approx(1.0).epsilon(1e-12));
            CHECK(a.rp * a.rp + k_t * a.tp * a.tp == doctest::Approx(1.0).epsilon(1e-12));
        }
        const auto a0 = fresnel_amplitudes(1.0, 1.0, n1, n2);
        CHECK(a0.rp == doctest::Approx(-a0.rs));   // this convention: rp = -rs at normal incidence
        CHECK(a0.rs == doctest::Approx((n1 - n2) / (n1 + n2)));
    }
    // Brewster's angle: rp vanishes at tan(theta) = n2 / n1.
    const double tb = std::atan(1.5);
    const double st = std::sin(tb) / 1.5;
    const auto   ab = fresnel_amplitudes(std::cos(tb), std::sqrt(1.0 - st * st), 1.0, 1.5);
    CHECK(std::fabs(ab.rp) < 1e-12);
}