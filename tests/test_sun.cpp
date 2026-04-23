#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/surfaces/Sphere.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cmath>
#include <memory>

using namespace scrt::math;
using namespace scrt::core;

// T5 -----------------------------------------------------------------------

TEST_CASE("T5: ray-sphere intersection at predicted t-values") {
    scrt::surfaces::Sphere sphere(1.0);  // radius 1, identity transform

    Ray r;
    r.origin    = {0.0, 0.0, -5.0};
    r.direction = {0.0, 0.0,  1.0};

    Hit hit;
    CHECK(sphere.intersect(r, 1e-6, 1e9, hit));
    // Front face at t=4, back face at t=6
    CHECK(hit.t == doctest::Approx(4.0).epsilon(1e-12));

    // Second hit: flip normal direction so back face is visible by shifting origin inside
    Ray r2;
    r2.origin    = {0.0, 0.0, 0.0};  // inside sphere
    r2.direction = {0.0, 0.0, 1.0};
    Hit hit2;
    CHECK(sphere.intersect(r2, 1e-6, 1e9, hit2));
    CHECK(hit2.t == doctest::Approx(1.0).epsilon(1e-12));
}

TEST_CASE("T5: ray misses sphere") {
    scrt::surfaces::Sphere sphere(1.0);
    Ray r;
    r.origin    = {0.0, 5.0, -5.0};
    r.direction = {0.0, 0.0,  1.0};
    Hit hit;
    CHECK_FALSE(sphere.intersect(r, 1e-6, 1e9, hit));
}

// T6 -----------------------------------------------------------------------

TEST_CASE("T6: parallel pencil on paraboloid reflects through focus") {
    // f=0.5 m, focus at (0,0,0.5). Rays D=(0,0,1) from below.
    scrt::surfaces::Paraboloid para(0.5, 0.45);  // aperture < 0.5m
    scrt::materials::PerfectMirror mirror;
    para.set_material(&mirror);

    Rng rng(42);
    const double f = 0.5;

    // Test several off-axis positions
    for (double r0 : {0.05, 0.1, 0.2, 0.3, 0.4}) {
        for (double phi : {0.0, 1.0, 2.0, 3.0, 4.0}) {
            double x0 = r0 * std::cos(phi);
            double y0 = r0 * std::sin(phi);

            Ray ray;
            ray.origin    = {x0, y0, -1.0};
            ray.direction = {0.0, 0.0, 1.0};

            Hit hit;
            REQUIRE(para.intersect(ray, 1e-6, 1e9, hit));

            // Reflected direction
            auto inter = mirror.interact(ray, hit, rng);
            vec3 d = inter.reflected.direction;

            // Parametric line from hit.position in direction d should pass through (0,0,f)
            // t when x=0: t = -hit.position.x / d.x
            if (std::abs(d.x) > 1e-12) {
                double t = -hit.position.x / d.x;
                double z_at_focus = hit.position.z + t * d.z;
                CHECK(z_at_focus == doctest::Approx(f).epsilon(1e-8));
                double y_at_focus = hit.position.y + t * d.y;
                CHECK(std::abs(y_at_focus) < 1e-8);
            } else {
                // On-axis: x0≈0 path; check via y
                double t = -hit.position.y / d.y;
                double z_at_focus = hit.position.z + t * d.z;
                CHECK(z_at_focus == doctest::Approx(f).epsilon(1e-8));
            }
        }
    }
}

// T10 -----------------------------------------------------------------------

TEST_CASE("T10: paraboloid f=1 + pillbox sun -> focal spot D90 within 5%") {
    const double f       = 1.0;
    const double ap_r    = 0.5;   // aperture radius [m]
    const double half_a  = 4.65e-3;  // solar half-angle [rad]

    // Build scene
    scrt::scene::Scene scene;

    // Mirror material
    auto mirror_mat = std::make_unique<scrt::materials::PerfectMirror>();
    auto* mirror_ptr = mirror_mat.get();
    scene.add_material(std::move(mirror_mat));

    // Absorber material (for receiver)
    auto absorber_mat = std::make_unique<scrt::materials::Absorber>();
    auto* absorber_ptr = absorber_mat.get();
    scene.add_material(std::move(absorber_mat));

    // Paraboloid: f=1m, aperture 0.5m, identity transform (vertex at origin)
    auto dish = std::make_unique<scrt::surfaces::Paraboloid>(f, ap_r);
    dish->set_material(mirror_ptr);
    scene.add_surface(std::move(dish));

    // Receiver: plane at z=f, half-extents 0.025m, 100x100 grid
    const double recv_hw = 0.025;
    const int    recv_n  = 100;
    auto recv = std::make_unique<scrt::scene::Receiver>(recv_hw, recv_hw, recv_n, recv_n);
    recv->surface()->set_material(absorber_ptr);
    recv->set_transform(Transform::from_translation({0.0, 0.0, f}));
    scene.set_receiver(std::move(recv));

    // Sun: pillbox, direction = (0,0,-1) (downward)
    auto sun = std::make_unique<scrt::sources::Pillbox>(half_a);
    sun->set_sun_direction({0.0, 0.0, -1.0});
    sun->set_dni(1000.0);
    scene.set_sun(std::move(sun));

    // Aperture: disk above dish, normal pointing up toward sun
    scrt::scene::Aperture ap;
    ap.center = {0.0, 0.0, 2.0};
    ap.normal = {0.0, 0.0,  1.0};
    ap.radius = ap_r;
    scene.set_aperture(ap);

    // Trace
    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = 1'000'000;
    cfg.max_bounces    = 4;
    cfg.rng_seed       = 12345;

    scrt::tracer::Tracer tracer(scene);
    scrt::tracer::FluxAccumulator& acc = scene.receiver()->accumulator();
    tracer.run(cfg, acc);

    // Expected focal spot: D90 = 2*f*tan(half_a) ≈ 2*f*half_a
    double d90_expected = 2.0 * f * std::tan(half_a);
    double d90_measured = acc.encircled_diameter(0.90);

    // Tolerance: within 5% of expected. For a uniform disk source, measured D90
    // ≈ sqrt(0.9)*D_full ≈ 0.949 * d90_expected, well within 5% if we compare
    // to the full analytical diameter.
    // Check both that power actually reached the receiver and spot size is plausible.
    CHECK(acc.total_power_w() > 0.0);
    CHECK(d90_measured > 0.0);
    CHECK(d90_measured == doctest::Approx(d90_expected).epsilon(0.05));
}
