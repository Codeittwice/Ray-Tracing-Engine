#include <doctest/doctest.h>
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/tracer/EmissionPlan.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cmath>
#include <memory>
#include <stdexcept>
#include <string>

using scrt::math::vec3;

TEST_CASE("Laser: setters reject what names no beam") {
    scrt::sources::Laser l;
    CHECK(std::string(l.type_name()) == "laser");
    CHECK(l.as_sun() == nullptr);

    CHECK_THROWS_AS(l.set_direction({0.0, 0.0, 0.0}), std::invalid_argument);
    CHECK_THROWS_AS(l.set_power_w(-1.0), std::invalid_argument);
    CHECK_THROWS_AS(l.set_power_w(std::nan("")), std::invalid_argument);
    CHECK_THROWS_AS(l.set_wavelength_nm(0.0), std::invalid_argument);
    CHECK_THROWS_AS(l.set_beam_diameter_m(-0.001), std::invalid_argument);
    CHECK_THROWS_AS(l.set_divergence_mrad(-1.0), std::invalid_argument);
    // Above a 180-degree full angle part of the cap lies behind the emitter; refused.
    CHECK_THROWS_AS(l.set_divergence_mrad(7000.0), std::invalid_argument);
    CHECK_NOTHROW(l.set_divergence_mrad(3000.0));

    l.set_power_w(0.0);   // Switched off is a valid state; the plan drops it.
    CHECK(l.total_power_w() == 0.0);
    l.set_direction({0.0, 3.0, 0.0});
    CHECK(l.direction().y == 1.0);
}

TEST_CASE("Laser: rays leave the beam disk inside the divergence cone, weight 1, wavelength set") {
    scrt::sources::Laser l;
    l.set_origin({0.2, -0.1, 0.5});
    l.set_direction({0.0, 0.0, -1.0});
    l.set_power_w(3.0);
    l.set_wavelength_nm(1064.0);
    l.set_beam_diameter_m(0.004);
    l.set_divergence_mrad(2.0);   // full angle -> 1 mrad half-angle

    scrt::math::Rng rng(5);
    const double half = 1.0e-3;
    for (int i = 0; i < 500; ++i) {
        const auto r = l.sample_ray(rng);
        CHECK(r.power == 1.0);
        CHECK(r.wavelength_nm == 1064.0);
        // On the disk: in the plane through the origin, within the beam radius.
        const vec3 d = r.origin - l.origin();
        CHECK(std::abs(d.z) < 1e-15);
        CHECK(glm::length(d) <= 0.002 + 1e-15);
        // Within the cone, and a unit vector.
        CHECK(std::abs(glm::length(r.direction) - 1.0) < 1e-12);
        CHECK(glm::dot(r.direction, l.direction()) >= std::cos(half) - 1e-12);
    }
    CHECK(l.total_power_w() == 3.0);
}

TEST_CASE("Laser: the cone is uniform over the spherical cap, not the small-angle disk") {
    // For cos(theta) uniform on [cos(half), 1] the mean is (1 + cos(half)) / 2. At a 120-degree
    // full angle the small-angle form theta = half*sqrt(xi) would give a mean cos of ~0.60
    // rather than 0.75, well outside the Monte Carlo tolerance below.
    scrt::sources::Laser l;
    l.set_direction({0.0, 0.0, -1.0});
    l.set_divergence_mrad(2.0 * 60.0 * scrt::math::DEG2RAD * 1e3);
    scrt::math::Rng rng(17);
    double sum = 0.0;
    const int n = 40000;
    for (int i = 0; i < n; ++i)
        sum += glm::dot(l.sample_ray(rng).direction, l.direction());
    CHECK(sum / n == doctest::Approx(0.75).epsilon(0.01));
}

namespace {

/// Absorbing plate of half-width `hw` at height z, with a fresh scene around it.
struct PlateScene {
    std::unique_ptr<scrt::scene::Scene> scene = std::make_unique<scrt::scene::Scene>();
    scrt::materials::Absorber*          absorber = nullptr;

    PlateScene(double hw, double z, int n) {
        auto a   = std::make_unique<scrt::materials::Absorber>();
        absorber = a.get();
        scene->add_material(std::move(a));
        auto recv = std::make_unique<scrt::scene::Receiver>(hw, hw, n, n);
        recv->surface()->set_material(absorber);
        recv->set_transform(scrt::core::Transform::from_translation({0.0, 0.0, z}));
        scene->set_receiver(std::move(recv));
    }
};

double traced_total_w(scrt::scene::Scene& scene, std::size_t n_rays, std::uint64_t seed) {
    scene.build_acceleration_structure();
    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = n_rays;
    cfg.rng_seed       = seed;
    cfg.num_threads    = 1;
    cfg.max_bounces    = 4;
    scrt::tracer::Tracer tracer(scene);
    tracer.run(cfg);
    return scene.receiver()->accumulator().total_power_w();
}

} // namespace

TEST_CASE("Laser: a pencil beam delivers exactly its authored watts to the plate") {
    PlateScene ps(0.5, 0.0, 16);
    auto l = std::make_unique<scrt::sources::Laser>();
    l->set_origin({0.0, 0.0, 1.0});
    l->set_direction({0.0, 0.0, -1.0});
    l->set_power_w(5.0);
    l->set_beam_diameter_m(0.0);
    l->set_divergence_mrad(0.0);
    ps.scene->add_source(std::move(l));

    // N copies of 5/N summed: equal to 5 up to floating summation, which is all that is claimed.
    CHECK(traced_total_w(*ps.scene, 1000, 9) == doctest::Approx(5.0).epsilon(1e-12));
}

TEST_CASE("Laser and sun in one scene: the plan holds both and the plate collects the sum") {
    PlateScene ps(0.5, 0.0, 16);

    auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
    sun->set_sun_direction({0.0, 0.0, -1.0});
    sun->set_dni(1000.0);
    scrt::scene::Aperture ap;
    ap.center = {0.0, 0.0, 1.0};
    ap.radius = 0.5;   // inscribed in the plate: every ray lands
    sun->set_aperture(ap);
    const double sun_w = sun->total_power_w();
    ps.scene->add_source(std::move(sun));

    auto l = std::make_unique<scrt::sources::Laser>();
    l->set_origin({0.1, 0.1, 1.0});
    l->set_direction({0.0, 0.0, -1.0});
    l->set_power_w(5.0);
    l->set_beam_diameter_m(0.0);
    ps.scene->add_source(std::move(l));

    const auto plan = scrt::tracer::build_emission_plan(ps.scene->sources(), 10000);
    REQUIRE(plan.entries.size() == 2);
    CHECK(plan.entries[0].source->as_sun() != nullptr);
    CHECK(plan.entries[1].source->as_sun() == nullptr);
    CHECK(plan.total_rays() == 10000);

    CHECK(traced_total_w(*ps.scene, 10000, 21) == doctest::Approx(sun_w + 5.0).epsilon(1e-9));
    CHECK(ps.scene->primary_sun() != nullptr);   // the legacy paths still find their sun
}

TEST_CASE("Laser wavelength reaches Dielectric::n_at through the tracer: Snell at two colours") {
    // The gap the goldens cannot close: no golden scene is dispersive, so a source stamping
    // the wrong wavelength would pass every golden check. Here a pencil beam at 45 degrees
    // enters a BK7 (Sellmeier) plane at z = 0 and lands on a plate at z = -1. Where it lands
    // is 1 + tan(asin(sin(45) / n(lambda))), read from the recorded path, so the wavelength
    // must have travelled source -> Ray -> Tracer -> Dielectric::n_at unchanged.
    auto land_x = [](double wavelength_nm, double& n_out) {
        PlateScene ps(5.0, -1.0, 8);

        auto glass = std::make_unique<scrt::materials::Dielectric>(1.5, 0.0);
        glass->set_sellmeier(scrt::materials::SellmeierCoeffs::bk7());
        n_out = glass->n_at(wavelength_nm);
        auto* glass_ptr = glass.get();
        ps.scene->add_material(std::move(glass));
        auto slab = std::make_unique<scrt::surfaces::Plane>(5.0, 5.0);   // z = 0, normal +Z
        slab->set_material(glass_ptr);
        ps.scene->add_surface(std::move(slab));

        auto l = std::make_unique<scrt::sources::Laser>();
        l->set_origin({0.0, 0.0, 1.0});
        l->set_direction({1.0, 0.0, -1.0});
        l->set_power_w(1.0);
        l->set_wavelength_nm(wavelength_nm);
        l->set_beam_diameter_m(0.0);
        l->set_divergence_mrad(0.0);
        ps.scene->add_source(std::move(l));
        ps.scene->build_acceleration_structure();

        scrt::tracer::TraceConfig cfg;
        cfg.n_primary_rays      = 4;
        cfg.rng_seed            = 3;
        cfg.num_threads         = 1;
        cfg.max_bounces         = 4;
        cfg.record_paths        = true;
        cfg.max_paths_to_record = 4;
        scrt::tracer::Tracer tracer(*ps.scene);
        const auto res = tracer.run(cfg);
        REQUIRE(!res.sampled_paths.empty());

        // The reflected branch flies up and hits nothing, so the only node at z = -1 is the
        // transmitted ray's landing point.
        for (const auto& p : res.sampled_paths[0].nodes)
            if (std::abs(p.z + 1.0) < 1e-9) return p.x;
        FAIL("no path node landed on the plate");
        return 0.0;
    };

    const double sin_i = std::sqrt(0.5);   // 45 degrees, from direction (1, 0, -1) normalised
    auto expected = [&](double n) { return 1.0 + std::tan(std::asin(sin_i / n)); };

    double n_blue = 0.0, n_ir = 0.0;
    const double x_blue = land_x(400.0, n_blue);
    const double x_ir   = land_x(1064.0, n_ir);

    CHECK(n_blue > n_ir);   // normal dispersion: BK7 bends blue more
    CHECK(x_blue == doctest::Approx(expected(n_blue)).epsilon(1e-9));
    CHECK(x_ir   == doctest::Approx(expected(n_ir)).epsilon(1e-9));
    CHECK(x_ir - x_blue == doctest::Approx(expected(n_ir) - expected(n_blue)).epsilon(1e-6));
    CHECK(x_ir - x_blue > 1e-3);   // about a centimetre over a metre of drop: not noise
}

TEST_CASE("Laser grid sampling: equal-area, deterministic, inside the beam, and draws nothing") {
    scrt::sources::Laser l;
    l.set_origin({0.0, 0.0, 0.0});
    l.set_direction({0.0, 0.0, 1.0});
    l.set_beam_diameter_m(0.01);
    l.set_divergence_mrad(0.0);
    l.set_sampling(scrt::sources::Laser::Sampling::Grid);
    CHECK(l.deterministic_sampling());

    const std::size_t n = 20000;
    scrt::math::Rng   rng(42), untouched(42);
    std::size_t       inner = 0;
    for (std::size_t k = 0; k < n; ++k) {
        const auto r  = l.sample_ray_indexed(rng, k, n);
        const double rr = std::hypot(r.origin.x, r.origin.y);
        CHECK(rr <= 0.005 + 1e-12);
        if (rr < 0.0025) ++inner;
        const auto again = l.sample_ray_indexed(rng, k, n);
        CHECK(again.origin == r.origin);
    }
    // Equal area per point: a quarter of the disk's area lies inside half its radius.
    CHECK(static_cast<double>(inner) / n == doctest::Approx(0.25).epsilon(0.002));
    // No Rng draws: the generator is exactly where an unused one with the same seed is.
    CHECK(rng.uniform01() == untouched.uniform01());

    // Random mode is sample_ray, draw for draw.
    l.set_sampling(scrt::sources::Laser::Sampling::Random);
    CHECK_FALSE(l.deterministic_sampling());
    scrt::math::Rng a(9), b(9);
    const auto ra = l.sample_ray_indexed(a, 3, 10);
    const auto rb = l.sample_ray(b);
    CHECK(ra.origin == rb.origin);
    CHECK(ra.direction == rb.direction);
}

TEST_CASE("power cutoff: relative to each primary ray, so a milliwatt laser keeps its ghosts") {
    // QA 15 (5 mW through an n = 1.5 window at normal incidence) with its explicit 1e-15 W override
    // REMOVED, i.e. the default absolute 1e-9 W. Split over 50k rays each ray starts at 1e-7 W, so every
    // internal ghost (x 0.0016) used to fall under the absolute cutoff and the screen read 92.16%. The
    // relative cutoff keeps them, and the full incoherent series (1-R)^2 / (1-R^2) = 92.308% is read.
    auto doc = scrt::io::parse_document(
        nlohmann::json::parse(std::ifstream(std::filesystem::path(SCRT_SOURCE_DIR) / "examples" /
                                            "feature_checks" / "qa_15_window_normal_incidence.json")),
        true);
    doc.trace.power_cutoff_w = 1e-9;
    auto ls = scrt::io::build_scene(doc, std::filesystem::path(SCRT_SOURCE_DIR) / "examples" / "feature_checks");
    ls.scene->build_acceleration_structure();
    ls.cfg.num_threads = 4;
    scrt::tracer::Tracer(*ls.scene).run(ls.cfg);
    const double R      = 0.04;
    const double expect = 0.005 * (1.0 - R) * (1.0 - R) / (1.0 - R * R);
    CHECK(std::fabs(ls.scene->receiver()->accumulator().total_power_w() / expect - 1.0) < 1e-6);
}
