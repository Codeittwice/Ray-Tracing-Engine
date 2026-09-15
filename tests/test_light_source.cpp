#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Buie.hpp"
#include "scrt/sources/LightSource.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cstddef>
#include <memory>
#include <stdexcept>
#include <string>

// Wave 2 Stage 1: the sun owns its aperture and states its power in watts. Every check
// here that can be exact IS exact (==, not Approx): these are the invariants that keep the
// golden flux baselines still, and a tolerance would hide a last-bit drift.

TEST_CASE("SunSource::total_power_w is DNI * area * cosine, bit for bit") {
    // The tracer used to compute sun.dni() * ap.area() * ap.cosine_to(-sun_direction) / N
    // itself. Now the sun computes the numerator; same members, same left-to-right
    // association, so it must be the same double, not merely a close one.
    scrt::sources::Pillbox sun(scrt::math::SOLAR_HALF_ANGLE_RAD);

    SUBCASE("zenith, the configuration every shipped scene uses") {
        sun.set_sun_direction({0.0, 0.0, -1.0});
        sun.set_dni(1000.0);
        scrt::scene::Aperture ap;  // normal +Z, radius 1
        ap.radius = 0.73;
        sun.set_aperture(ap);
        CHECK(sun.aperture().cosine_to(sun.to_sun()) == 1.0);
        CHECK(sun.total_power_w() == 1000.0 * ap.area() * ap.cosine_to(sun.to_sun()));
    }

    SUBCASE("oblique sun over a fixed horizontal disk") {
        sun.set_sun_angles({135.0, 32.0});
        sun.set_dni(842.0);
        scrt::scene::Aperture ap;
        ap.normal = {0.0, 0.0, 1.0};
        ap.radius = 1.7;
        sun.set_aperture(ap);
        const double expected = 842.0 * ap.area() * ap.cosine_to(sun.to_sun());
        CHECK(sun.total_power_w() == expected);
        CHECK(sun.total_power_w() < 842.0 * ap.area());  // foreshortened, not full
    }

    SUBCASE("a sun that has set collects nothing") {
        sun.set_sun_angles({180.0, -10.0});
        scrt::scene::Aperture ap;
        sun.set_aperture(ap);
        CHECK(sun.total_power_w() == 0.0);
    }
}

TEST_CASE("Sun rays carry weight exactly 1.0 and wavelength exactly 550 nm") {
    // Dielectric::n_at reads Ray::wavelength_nm for Sellmeier dispersion. No golden scene is
    // dispersive, so the goldens would sit still while a source quietly started emitting
    // 1064 nm; this is the assertion that catches it. 550.0 equals core::Ray's own default,
    // so the stamp writes the double that was already there.
    scrt::math::Rng rng(7);
    scrt::scene::Aperture ap;

    scrt::sources::Pillbox pill(4.65e-3);
    pill.set_aperture(ap);
    scrt::sources::Buie buie(0.05);
    buie.set_aperture(ap);

    for (int i = 0; i < 64; ++i) {
        const auto rp = pill.sample_ray(rng);
        CHECK(rp.power == 1.0);
        CHECK(rp.wavelength_nm == 550.0);
        const auto rb = buie.sample_ray(rng);
        CHECK(rb.power == 1.0);
        CHECK(rb.wavelength_nm == 550.0);
    }
    CHECK(std::string(pill.type_name()) == "sun");
    CHECK(pill.as_sun() == &pill);

    const scrt::sources::LightSource& base = pill;
    CHECK(base.as_sun() == &pill);
    CHECK(base.total_power_w() == pill.total_power_w());
}

TEST_CASE("FluxAccumulator::finalize ignores its ray count, and must keep doing so") {
    // Every ray arrives pre-divided by the ray count (total_power_w() / N at emission), so
    // the accumulated power IS watts and finalize only divides by bin area. With several
    // sources the pre-division composes; a finalize that divided by N again would not.
    // This will look like a bug to a reader who has just learned there are several sources.
    // "Fixing" it would move every golden value by six orders of magnitude. Bit-identical.
    scrt::tracer::FluxAccumulator a(0.5, 0.5, 8, 8);
    scrt::tracer::FluxAccumulator b(0.5, 0.5, 8, 8);

    scrt::math::Rng rng(3);
    for (int i = 0; i < 500; ++i) {
        scrt::core::Ray r;
        r.power = 0.001 * rng.uniform01();
        scrt::core::Hit h;
        h.uv = {rng.uniform01() - 0.5, rng.uniform01() - 0.5};
        a.deposit(r, h);
        b.deposit(r, h);
    }
    a.finalize(1);
    b.finalize(1'000'000);

    CHECK(a.total_power_w() == b.total_power_w());
    CHECK(a.peak_flux_wm2() == b.peak_flux_wm2());
    REQUIRE(a.flux_map_wm2().size() == b.flux_map_wm2().size());
    for (std::size_t i = 0; i < a.flux_map_wm2().size(); ++i)
        CHECK(a.flux_map_wm2()[i] == b.flux_map_wm2()[i]);
}

TEST_CASE("Scene::display_aperture follows the sun; Tracer tolerates no sun at all") {
    scrt::scene::Scene scene;
    CHECK(scene.display_aperture() == nullptr);

    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* absorber_ptr = absorber.get();
    scene.add_material(std::move(absorber));
    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 4, 4);
    recv->surface()->set_material(absorber_ptr);
    scene.set_receiver(std::move(recv));
    scene.build_acceleration_structure();

    // Tracer.cpp used to dereference scene->sun() unguarded while three of its callers
    // checked for null. A scene with a receiver and no source now traces to nothing.
    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = 100;
    cfg.rng_seed       = 1;
    cfg.num_threads    = 1;
    scrt::tracer::Tracer tracer(scene);
    const auto res = tracer.run(cfg);
    CHECK(res.primary_rays_traced == 0);
    CHECK(res.total_hits == 0);
    // And the receiver is left finalized, so a reader sees zeros rather than stale state.
    for (double f : scene.receiver()->accumulator().flux_map_wm2()) CHECK(f == 0.0);
    CHECK_THROWS_AS(scene.add_source(nullptr), std::invalid_argument);

    auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
    scrt::scene::Aperture ap;
    ap.center = {0.1, 0.2, 3.0};
    ap.radius = 0.9;
    sun->set_aperture(ap);
    const auto* sun_raw = sun.get();
    scene.add_source(std::move(sun));
    CHECK(scene.primary_sun() == sun_raw);

    REQUIRE(scene.display_aperture() != nullptr);
    CHECK(scene.display_aperture() == &sun_raw->aperture());
    CHECK(scene.display_aperture()->radius == 0.9);
    CHECK(scene.display_aperture()->center.z == 3.0);
}
