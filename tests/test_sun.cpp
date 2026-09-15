#include <doctest/doctest.h>
#include "scrt/core/AABB.hpp"
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
#include "scrt/sources/SunSource.hpp"
#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/surfaces/Sphere.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cmath>
#include <cstddef>
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

    // Aperture: disk above dish, normal pointing up toward sun. The sun owns it.
    scrt::scene::Aperture ap;
    ap.center = {0.0, 0.0, 2.0};
    ap.normal = {0.0, 0.0,  1.0};
    ap.radius = ap_r;
    sun->set_aperture(ap);
    scene.set_sun(std::move(sun));

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

// Sun angles ----------------------------------------------------------------
//
// Convention under test: +Z is the zenith, elevation is measured up from the horizon,
// and azimuth is a compass bearing from +Y (North) increasing toward +X (East). The
// bearing origin is not arbitrary — SceneLoader places a box receiver's `north_wall`
// at +Y and its `east_wall` at +X.

using scrt::sources::SunAngles;
using scrt::sources::SunSource;

TEST_CASE("Sun angles: azimuth/elevation round-trip away from the pole") {
    const double azimuths[]   = {0.0, 15.0, 45.0, 90.0, 135.0, 180.0, 225.0, 270.0, 337.5};
    const double elevations[] = {-80.0, -45.0, -5.0, 0.0, 5.0, 23.5, 45.0, 66.5, 89.0};

    for (double az : azimuths) {
        for (double el : elevations) {
            const SunAngles a{az, el};
            const SunAngles r = SunSource::angles_from_direction(
                SunSource::direction_from_angles(a));
            // Absolute, not relative: az == 0 has no meaningful relative tolerance.
            CHECK(std::abs(r.azimuth_deg   - a.azimuth_deg)   < 1e-9);
            CHECK(std::abs(r.elevation_deg - a.elevation_deg) < 1e-9);
        }
    }
}

TEST_CASE("Sun angles: bearing runs from +Y (North) toward +X (East)") {
    // Sun on the horizon due north -> it sits at +Y, so light travels toward -Y.
    const vec3 north = SunSource::direction_from_angles({0.0, 0.0});
    CHECK(std::abs(north.x) < 1e-15);
    CHECK(north.y == doctest::Approx(-1.0).epsilon(1e-12));
    CHECK(std::abs(north.z) < 1e-15);

    // Sun on the horizon due east -> it sits at +X, so light travels toward -X.
    const vec3 east = SunSource::direction_from_angles({90.0, 0.0});
    CHECK(east.x == doctest::Approx(-1.0).epsilon(1e-12));
    CHECK(std::abs(east.y) < 1e-15);
    CHECK(std::abs(east.z) < 1e-15);
}

TEST_CASE("Sun angles: zenith is exactly (0,0,-1) for every azimuth") {
    for (double az : {0.0, 37.0, 90.0, 180.0, 271.5, 359.9}) {
        const vec3 d = SunSource::direction_from_angles({az, 90.0});
        // Exact, not approximate: every existing scene pins direction = (0,0,-1) and its
        // golden flux would shift if the zenith leaked a round-off-sized tilt.
        CHECK(d.x == 0.0);
        CHECK(d.y == 0.0);
        CHECK(d.z == -1.0);
    }
}

TEST_CASE("Sun angles: pole degeneracy returns the fallback azimuth, never NaN") {
    // Straight overhead: all azimuths name the same direction, so atan2 would hand back
    // an arbitrary value. The caller's fallback wins instead.
    const SunAngles up = SunSource::angles_from_direction({0.0, 0.0, -1.0}, 137.0);
    CHECK_FALSE(std::isnan(up.azimuth_deg));
    CHECK(up.azimuth_deg == 137.0);
    CHECK(up.elevation_deg == doctest::Approx(90.0).epsilon(1e-12));

    // Same story underfoot (sun below the horizon, light travelling up).
    const SunAngles down = SunSource::angles_from_direction({0.0, 0.0, 1.0}, 42.0);
    CHECK(down.azimuth_deg == 42.0);
    CHECK(down.elevation_deg == doctest::Approx(-90.0).epsilon(1e-12));

    // Default fallback is due south.
    CHECK(SunSource::angles_from_direction({0.0, 0.0, -1.0}).azimuth_deg == 180.0);

    // Round-tripping the zenith is still degeneracy, not an error.
    const SunAngles rt = SunSource::angles_from_direction(
        SunSource::direction_from_angles({77.0, 90.0}), 180.0);
    CHECK(rt.azimuth_deg == 180.0);
}

TEST_CASE("Sun angles: to_sun is the opposite of the propagation direction") {
    scrt::sources::Pillbox sun(scrt::math::SOLAR_HALF_ANGLE_RAD);
    sun.set_sun_angles({135.0, 40.0});
    const vec3 d = sun.sun_direction();
    const vec3 s = sun.to_sun();
    CHECK(s.x == doctest::Approx(-d.x).epsilon(1e-15));
    CHECK(s.y == doctest::Approx(-d.y).epsilon(1e-15));
    CHECK(s.z == doctest::Approx(-d.z).epsilon(1e-15));
    CHECK(s.z > 0.0);  // Sun above the horizon at elevation 40.
}

// Aperture cosine and auto-fit ----------------------------------------------

TEST_CASE("Aperture: cosine_to is exactly 1 for the legacy zenith configuration") {
    scrt::scene::Aperture ap;  // Defaults: normal = +Z, the value every bundled scene uses.
    CHECK(ap.mode == scrt::scene::ApertureMode::Fixed);

    scrt::sources::Pillbox sun(scrt::math::SOLAR_HALF_ANGLE_RAD);
    sun.set_sun_direction({0.0, 0.0, -1.0});
    // Exact 1.0: this is the invariant that keeps the golden flux baselines still.
    CHECK(ap.cosine_to(sun.to_sun()) == 1.0);
}

TEST_CASE("Aperture: cosine_to foreshortens and clamps a back-facing disk") {
    scrt::scene::Aperture ap;
    ap.normal = {0.0, 0.0, 1.0};

    CHECK(ap.cosine_to({0.0, 0.0, -1.0}) == 0.0);   // Facing straight away.
    CHECK(ap.cosine_to({1.0, 0.0, 0.0}) == 0.0);    // Edge-on.
    CHECK(ap.cosine_to({0.0, -0.6, -0.8}) == 0.0);  // Clamped, never negative.

    for (double el : {15.0, 30.0, 45.0, 60.0, 75.0}) {
        const vec3 to_sun = -SunSource::direction_from_angles({180.0, el});
        CHECK(ap.cosine_to(to_sun) ==
              doctest::Approx(std::sin(el * scrt::math::DEG2RAD)).epsilon(1e-12));
    }

    // An unnormalized normal must not scale the cosine.
    scrt::scene::Aperture scaled;
    scaled.normal = {0.0, 0.0, 7.0};
    CHECK(scaled.cosine_to({0.0, 0.0, 1.0}) == doctest::Approx(1.0).epsilon(1e-15));
}

TEST_CASE("Aperture: auto_fit faces the sun and covers the bounds") {
    const scrt::core::AABB bounds({-1.0, -0.5, -0.2}, {1.0, 0.5, 0.6});
    const double R = 0.5 * glm::length(bounds.max() - bounds.min());

    for (double az : {0.0, 90.0, 180.0, 300.0}) {
        for (double el : {20.0, 45.0, 70.0, 90.0}) {
            const vec3 to_sun = -SunSource::direction_from_angles({az, el});
            const scrt::scene::Aperture ap =
                scrt::scene::Aperture::auto_fit(bounds, to_sun, 0.05);

            CHECK(ap.mode == scrt::scene::ApertureMode::AutoFitToSun);
            CHECK(ap.margin == 0.05);
            CHECK(ap.cosine_to(to_sun) == doctest::Approx(1.0).epsilon(1e-12));
            CHECK(ap.covers(bounds, to_sun));
            CHECK(ap.radius == doctest::Approx(R * 1.05).epsilon(1e-12));
            // Disk sits clear of the geometry, on the sunward side.
            CHECK(glm::dot(ap.center - bounds.centroid(), to_sun) ==
                  doctest::Approx(R * 1.05).epsilon(1e-12));
        }
    }
}

TEST_CASE("Aperture: covers() rejects an undersized or misaimed disk") {
    const scrt::core::AABB bounds({-1.0, -1.0, 0.0}, {1.0, 1.0, 0.0});
    const vec3 to_sun{0.0, 0.0, 1.0};

    const scrt::scene::Aperture good = scrt::scene::Aperture::auto_fit(bounds, to_sun, 0.05);
    CHECK(good.covers(bounds, to_sun));

    scrt::scene::Aperture small = good;
    small.radius = 0.5;  // Smaller than the box half-diagonal.
    CHECK_FALSE(small.covers(bounds, to_sun));

    scrt::scene::Aperture behind = good;
    behind.center = bounds.centroid() - to_sun * 5.0;  // Disk under the geometry.
    CHECK_FALSE(behind.covers(bounds, to_sun));

    // Fitted for one sun, queried against another: the geometry falls out of the shadow.
    const vec3 low = -SunSource::direction_from_angles({180.0, 5.0});
    CHECK_FALSE(good.covers(bounds, low));
}

// Scene bounds ---------------------------------------------------------------

TEST_CASE("Scene::world_bounds unions surfaces and receiver faces") {
    scrt::scene::Scene empty;
    const auto zero = empty.world_bounds();
    CHECK(zero.min() == vec3{0.0});
    CHECK(zero.max() == vec3{0.0});

    scrt::scene::Scene scene;
    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* absorber_ptr = absorber.get();
    scene.add_material(std::move(absorber));

    // Receiver only: bounds must already be non-degenerate even though the receiver is
    // NOT part of the BVH, so a surfaces-only walk would return the inside-out box.
    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 8, 8);
    recv->surface()->set_material(absorber_ptr);
    recv->set_transform(Transform::from_translation({0.0, 0.0, -3.0}));
    scene.set_receiver(std::move(recv));

    const auto recv_only = scene.world_bounds();
    CHECK(recv_only.min().x == doctest::Approx(-0.5).epsilon(1e-12));
    CHECK(recv_only.max().x == doctest::Approx(0.5).epsilon(1e-12));
    CHECK(recv_only.centroid().z == doctest::Approx(-3.0).epsilon(1e-9));

    // Adding a dish above must grow the union upward while keeping the receiver's floor.
    auto dish = std::make_unique<scrt::surfaces::Paraboloid>(1.0, 0.9);
    dish->set_material(absorber_ptr);
    scene.add_surface(std::move(dish));

    const auto both = scene.world_bounds();
    CHECK(both.min().z <= recv_only.min().z);
    CHECK(both.max().z >= 0.0);
    CHECK(both.max().x >= 0.9);
    CHECK(both.min().x <= -0.9);
}

// End-to-end aperture cosine -------------------------------------------------

namespace {

/// Trace a flat 1 m^2 absorbing plate at z=0 under a collimated sun; returns absorbed W.
double flat_plate_absorbed_w(double azimuth_deg, double elevation_deg,
                             std::size_t n_rays, double dni_wm2) {
    scrt::scene::Scene scene;

    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* absorber_ptr = absorber.get();
    scene.add_material(std::move(absorber));

    // 1 m x 1 m plate, normal +Z (identity transform), single absorbing receiver face.
    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 64, 64);
    recv->surface()->set_material(absorber_ptr);
    scene.set_receiver(std::move(recv));

    // Zero half-angle: a perfectly collimated beam, so the only things under test are the
    // aperture cosine and the plate's projected area — no solar-cone blur to budget for.
    auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
    sun->set_sun_angles({azimuth_deg, elevation_deg});
    sun->set_dni(dni_wm2);
    sun->set_aperture(
        scrt::scene::Aperture::auto_fit(scene.world_bounds(), sun->to_sun(), 0.05));
    scene.set_sun(std::move(sun));
    scene.build_acceleration_structure();

    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = n_rays;
    cfg.max_bounces    = 4;
    cfg.rng_seed       = 20260907;
    cfg.record_paths   = false;

    scrt::tracer::Tracer tracer(scene);
    tracer.run(cfg);

    double total_w = 0.0;
    for (const auto& face : scene.receiver()->faces())
        total_w += face->accumulator().total_power_w();
    return total_w;
}

/// Same plate, but with a FIXED horizontal aperture that the sun strikes obliquely.
///
/// auto_fit always yields cosine_to == 1, so the auto-fitted case above cannot tell a
/// present cosine factor from a missing one. Here the disk keeps normal +Z while the sun
/// sits at `elevation_deg`, so cos_ap == sin(elevation) and the factor is load-bearing:
/// drop it and every elevation returns DNI * A instead of DNI * A * sin(elevation).
double flat_plate_fixed_aperture_absorbed_w(double azimuth_deg, double elevation_deg,
                                            std::size_t n_rays, double dni_wm2) {
    const double z0     = 3.0;   // Aperture height above the plate [m].
    const double ap_r   = 0.75;  // > plate half-diagonal (0.7071 m), so the plate's
                                 // footprint on the aperture plane fits entirely inside.

    scrt::scene::Scene scene;

    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* absorber_ptr = absorber.get();
    scene.add_material(std::move(absorber));

    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 64, 64);
    recv->surface()->set_material(absorber_ptr);
    scene.set_receiver(std::move(recv));

    auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
    sun->set_sun_angles({azimuth_deg, elevation_deg});
    sun->set_dni(dni_wm2);
    const vec3 to_sun = sun->to_sun();

    // Projecting the plate along -to_sun onto the horizontal aperture plane is a pure
    // translation by (z0 / sin(el)) * to_sun, so centre the disk on that shifted square.
    scrt::scene::Aperture ap;
    ap.mode   = scrt::scene::ApertureMode::Fixed;
    ap.normal = {0.0, 0.0, 1.0};
    ap.center = (z0 / to_sun.z) * to_sun;
    ap.radius = ap_r;
    sun->set_aperture(ap);
    scene.set_sun(std::move(sun));
    scene.build_acceleration_structure();

    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = n_rays;
    cfg.max_bounces    = 4;
    cfg.rng_seed       = 20260908;
    cfg.record_paths   = false;

    scrt::tracer::Tracer tracer(scene);
    tracer.run(cfg);

    double total_w = 0.0;
    for (const auto& face : scene.receiver()->faces())
        total_w += face->accumulator().total_power_w();
    return total_w;
}

} // namespace

TEST_CASE("End-to-end: absorbed power on a flat plate equals DNI * A * sin(elevation)") {
    const double      dni    = 1000.0;  // W/m^2
    const double      area   = 1.0;     // m^2 (0.5 m half-extents)
    const std::size_t n_rays = 1000000;

    // Tolerance: this is a binomial hit/miss experiment. The auto-fitted aperture has
    // radius R*1.05 with R = half the plate's bounding diagonal (~0.7071 m), so its area
    // is ~1.732 m^2 and the hit probability is p = A*sin(el)/1.732. The relative standard
    // error is sqrt((1-p)/(N*p)): 8.6e-4 at el=90, 1.0e-3 at el=60 and 1.6e-3 at el=30
    // for N = 1e6. A 1% band is therefore >= 6 sigma even in the worst case — loose enough
    // never to flake, tight enough that a missing or wrong cosine factor (a 13-100% error)
    // cannot hide inside it.
    const double tol = 0.01;

    for (double el : {90.0, 60.0, 30.0}) {
        const double expected = dni * area * std::sin(el * scrt::math::DEG2RAD);
        const double measured = flat_plate_absorbed_w(180.0, el, n_rays, dni);
        INFO("elevation_deg = ", el, "  expected_w = ", expected,
             "  measured_w = ", measured);
        CHECK(measured == doctest::Approx(expected).epsilon(tol));
    }

    // Projected area depends only on elevation, so a different bearing must not move it.
    const double expected45 = dni * area * std::sin(45.0 * scrt::math::DEG2RAD);
    for (double az : {0.0, 115.0, 250.0}) {
        const double measured = flat_plate_absorbed_w(az, 45.0, n_rays, dni);
        INFO("azimuth_deg = ", az, "  expected_w = ", expected45,
             "  measured_w = ", measured);
        CHECK(measured == doctest::Approx(expected45).epsilon(tol));
    }
}

TEST_CASE("End-to-end: a tilted fixed aperture still collects DNI * A * sin(elevation)") {
    const double      dni    = 1000.0;  // W/m^2
    const double      area   = 1.0;     // m^2
    const std::size_t n_rays = 1000000;

    // This is the case that actually pins the aperture cosine. The disk stays horizontal
    // (normal +Z) while the sun drops, so cos_ap = sin(el) < 1 and the emitted power is
    // DNI * pi * 0.75^2 * sin(el). The plate's footprint on the aperture plane is a unit
    // square wholly inside that disk, so the hit fraction is 1/(pi*0.75^2) = 0.5659 at
    // every elevation and the absorbed power collapses to DNI * A * sin(el).
    //
    // Delete the cos_ap term from Tracer and every elevation below returns 1000 W: a 15%
    // error at el=60 and a 100% error at el=30, far outside the band asserted here.
    //
    // Tolerance: binomial with p = 0.5659, so the relative standard error at N = 1e6 is
    // sqrt((1-p)/(N*p)) = 8.8e-4, independent of elevation. The 1% band is ~11 sigma.
    const double tol = 0.01;

    for (double el : {90.0, 60.0, 30.0, 15.0}) {
        const double expected = dni * area * std::sin(el * scrt::math::DEG2RAD);
        const double measured = flat_plate_fixed_aperture_absorbed_w(180.0, el, n_rays, dni);
        INFO("elevation_deg = ", el, "  expected_w = ", expected,
             "  measured_w = ", measured);
        CHECK(measured == doctest::Approx(expected).epsilon(tol));
    }
}
