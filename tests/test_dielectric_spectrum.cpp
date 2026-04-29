#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include <cmath>

// T_WA1 --------------------------------------------------------------------

TEST_CASE("T_WA1: alpha_at() falls back to scalar when no spectrum is set") {
    scrt::materials::Dielectric d(1.5, 42.0);
    CHECK(d.alpha_at(500.0) == doctest::Approx(42.0).epsilon(1e-12));
    CHECK(d.alpha_at(300.0) == doctest::Approx(42.0).epsilon(1e-12));
    CHECK(d.alpha_at(2000.0) == doctest::Approx(42.0).epsilon(1e-12));
}

TEST_CASE("T_WA1: alpha_at() piecewise-linear interpolation and endpoint clamping") {
    scrt::materials::Dielectric d(1.5, 99.0);  // scalar ignored once spectrum is set
    d.set_alpha_spectrum({{500.0, 0.0}, {700.0, 200.0}});

    // At nodes: exact values
    CHECK(d.alpha_at(500.0) == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(d.alpha_at(700.0) == doctest::Approx(200.0).epsilon(1e-12));

    // Midpoint (600 nm): linear interpolation → 100
    CHECK(d.alpha_at(600.0) == doctest::Approx(100.0).epsilon(1e-12));

    // Quarter-point (550 nm): linear interpolation → 50
    CHECK(d.alpha_at(550.0) == doctest::Approx(50.0).epsilon(1e-12));

    // Clamp below lower bound
    CHECK(d.alpha_at(300.0) == doctest::Approx(0.0).epsilon(1e-12));

    // Clamp above upper bound
    CHECK(d.alpha_at(900.0) == doctest::Approx(200.0).epsilon(1e-12));
}

TEST_CASE("T_WA1: set_alpha_spectrum accepts unsorted input and sorts internally") {
    scrt::materials::Dielectric d(1.5, 0.0);
    // Supply nodes in reverse order
    d.set_alpha_spectrum({{700.0, 200.0}, {500.0, 0.0}});
    CHECK(d.alpha_at(600.0) == doctest::Approx(100.0).epsilon(1e-12));
}

// T_WA2 --------------------------------------------------------------------

TEST_CASE("T_WA2: Beer-Lambert attenuation with alpha_spectrum matches exp(-alpha*t)") {
    // Setup: both dielectrics use n=1.5.  d_absorb has alpha=100/m at 500nm; d_clear is zero.
    // Exit hit (front_face=false) at normal incidence with path length 0.01 m.
    // Expected Beer factor: exp(-100 * 0.01) = exp(-1) ≈ 0.36788.
    // Both reflected and transmitted are attenuated by the same factor, so
    // ratio of total output powers equals the Beer factor regardless of Fresnel weights.

    scrt::materials::Dielectric d_absorb(1.5, 0.0);
    d_absorb.set_alpha_spectrum({{500.0, 100.0}});

    scrt::materials::Dielectric d_clear(1.5, 0.0);

    scrt::core::Ray r;
    r.direction     = {0.0, 0.0, 1.0};
    r.wavelength_nm = 500.0;
    r.power         = 1.0;
    r.bounces       = 0;

    scrt::core::Hit h;
    h.front_face = false;
    h.t          = 0.01;           // 1 cm path inside glass
    h.position   = {0.0, 0.0, 0.0};
    h.normal     = {0.0, 0.0, -1.0};  // oriented against incoming ray
    h.surface    = nullptr;

    scrt::math::Rng rng(0);
    auto ia_absorb = d_absorb.interact(r, h, rng);
    auto ia_clear  = d_clear.interact(r, h, rng);

    double p_absorb = ia_absorb.transmitted.power + ia_absorb.reflected.power;
    double p_clear  = ia_clear.transmitted.power  + ia_clear.reflected.power;

    CHECK(p_absorb / p_clear == doctest::Approx(std::exp(-100.0 * 0.01)).epsilon(1e-12));
}

TEST_CASE("T_WA2: alpha_spectrum at off-spectrum wavelength clamps and attenuates correctly") {
    // Spectrum has one node at 500nm with alpha=200. Wavelengths outside clamp to 200.
    scrt::materials::Dielectric d(1.5, 0.0);
    d.set_alpha_spectrum({{500.0, 200.0}});

    scrt::core::Ray r;
    r.direction     = {0.0, 0.0, 1.0};
    r.wavelength_nm = 800.0;  // above the only node: clamped to alpha=200
    r.power         = 1.0;
    r.bounces       = 0;

    scrt::core::Hit h;
    h.front_face = false;
    h.t          = 0.005;
    h.position   = {0.0, 0.0, 0.0};
    h.normal     = {0.0, 0.0, -1.0};
    h.surface    = nullptr;

    scrt::materials::Dielectric d_clear(1.5, 0.0);
    scrt::math::Rng rng(1);
    auto ia_d    = d.interact(r, h, rng);
    auto ia_clr  = d_clear.interact(r, h, rng);

    double ratio = (ia_d.transmitted.power + ia_d.reflected.power) /
                   (ia_clr.transmitted.power + ia_clr.reflected.power);
    CHECK(ratio == doctest::Approx(std::exp(-200.0 * 0.005)).epsilon(1e-12));
}
