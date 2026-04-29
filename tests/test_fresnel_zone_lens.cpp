#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/surfaces/FresnelZoneLens.hpp"
#include <cmath>

// Helper: build a downward ray at (x, y) aimed at the lens plane z=0.
static scrt::core::Ray make_ray(double x, double y, double wavelength_nm = 550.0) {
    scrt::core::Ray r;
    r.origin       = {x, y, 1.0};
    r.direction    = {0.0, 0.0, -1.0};
    r.power        = 1.0;
    r.wavelength_nm = wavelength_nm;
    return r;
}

// T_FZL1 -------------------------------------------------------------------

TEST_CASE("T_FZL1: FresnelZoneLens hit lies in z=0 plane") {
    scrt::surfaces::FresnelZoneLens lens(0.3, 0.01, 0.005, 22, 1.5);
    scrt::core::Hit hit;
    // Hit at mid-zone radius 0.055 (zone 8 midpoint: 0.01 + 8.5*0.005 = 0.0525)
    REQUIRE(lens.intersect(make_ray(0.055, 0.0), 1e-6, 1e9, hit));
    CHECK(hit.position.z == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(hit.position.x == doctest::Approx(0.055).epsilon(1e-12));
    CHECK(hit.front_face == true);
}

TEST_CASE("T_FZL1: ray misses inside inner_radius") {
    scrt::surfaces::FresnelZoneLens lens(0.3, 0.01, 0.005, 22, 1.5);
    scrt::core::Hit hit;
    CHECK_FALSE(lens.intersect(make_ray(0.005, 0.0), 1e-6, 1e9, hit));
}

TEST_CASE("T_FZL1: ray misses outside outer_radius") {
    scrt::surfaces::FresnelZoneLens lens(0.3, 0.01, 0.005, 22, 1.5);
    scrt::core::Hit hit;
    // outer = 0.01 + 22*0.005 = 0.12; 0.13 should miss
    CHECK_FALSE(lens.intersect(make_ray(0.13, 0.0), 1e-6, 1e9, hit));
}

TEST_CASE("T_FZL1: refracted ray converges to focal point (all zones, on-axis)") {
    // For each zone, fire a ray at the zone midpoint radius.
    // After refraction (using Dielectric), the ray must cross x=0 at z=-focal_length.
    const double f       = 0.3;
    const double inner_r = 0.01;
    const double pitch   = 0.005;
    const int    nzones  = 22;
    const double n       = 1.5;

    scrt::surfaces::FresnelZoneLens lens(f, inner_r, pitch, nzones, n);
    scrt::materials::Dielectric glass(n);
    lens.set_material(&glass);

    scrt::math::Rng rng(0);

    for (int i = 0; i < nzones; ++i) {
        double r_mid = inner_r + (i + 0.5) * pitch;

        scrt::core::Ray ray = make_ray(r_mid, 0.0);
        scrt::core::Hit hit;
        REQUIRE(lens.intersect(ray, 1e-6, 1e9, hit));

        auto ia = glass.interact(ray, hit, rng);
        // Transmitted ray should point toward (0, 0, -f)
        scrt::math::vec3 d = ia.transmitted.direction;
        scrt::math::vec3 o = hit.position;

        // Find t where z = -f along transmitted ray
        REQUIRE(std::abs(d.z) > 1e-10);
        double t_focal = (-f - o.z) / d.z;
        REQUIRE(t_focal > 0.0);

        double x_focal = o.x + t_focal * d.x;
        double y_focal = o.y + t_focal * d.y;

        // Must converge to x=0, y=0 (on-axis) within ~0.1mm (paraxial approximation)
        CHECK(std::abs(x_focal) < 1e-4);
        CHECK(std::abs(y_focal) < 1e-10);  // y=0 exactly (ray and lens are coplanar in x-z)
    }
}

TEST_CASE("T_FZL1: rotational symmetry — same zone at 45° converges to same focal point") {
    const double f = 0.3, inner_r = 0.01, pitch = 0.005;
    const double n = 1.5;
    scrt::surfaces::FresnelZoneLens lens(f, inner_r, pitch, 22, n);
    scrt::materials::Dielectric glass(n);

    const double r_mid = inner_r + 5.5 * pitch;  // zone 5
    const double phi   = 0.7853981633974483;       // 45 degrees

    scrt::core::Ray ray = make_ray(r_mid * std::cos(phi), r_mid * std::sin(phi));
    scrt::core::Hit hit;
    REQUIRE(lens.intersect(ray, 1e-6, 1e9, hit));

    scrt::math::Rng rng(3);
    auto ia = glass.interact(ray, hit, rng);
    scrt::math::vec3 d = ia.transmitted.direction;
    scrt::math::vec3 o = hit.position;

    double t_focal = (-f - o.z) / d.z;
    double x_focal = o.x + t_focal * d.x;
    double y_focal = o.y + t_focal * d.y;

    CHECK(std::abs(x_focal) < 1e-4);
    CHECK(std::abs(y_focal) < 1e-4);
}

// T_FZL2 -------------------------------------------------------------------

TEST_CASE("T_FZL2: energy conservation at each zone (R + T = 1)") {
    // Fresnel equations satisfy R + T = 1 by construction.
    // Verify that reflected.power + transmitted.power == incoming power
    // (Beer-Lambert is for exit hits; here front_face=true so no attenuation).
    const double f = 0.3, inner_r = 0.01, pitch = 0.005;
    const double n = 1.5;
    scrt::surfaces::FresnelZoneLens lens(f, inner_r, pitch, 22, n);
    scrt::materials::Dielectric glass(n);
    scrt::math::Rng rng(5);

    for (int i = 0; i < 22; ++i) {
        double r_mid = inner_r + (i + 0.5) * pitch;
        scrt::core::Ray ray = make_ray(r_mid, 0.0);
        ray.power = 2.5;

        scrt::core::Hit hit;
        REQUIRE(lens.intersect(ray, 1e-6, 1e9, hit));
        auto ia = glass.interact(ray, hit, rng);

        double total = ia.reflected.power + ia.transmitted.power;
        CHECK(total == doctest::Approx(ray.power).epsilon(1e-12));
    }
}
