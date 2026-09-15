#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Diffuser.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cmath>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

// Wave 3 Stage 5: Lambertian scattering, and with it gap G1 — a partly absorbing surface is a
// low-albedo diffuser, not a new absorber type.

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using scrt::math::vec3;

TEST_CASE("Diffuser: scatters into the hemisphere with Lambert's cosine law") {
    scrt::materials::Diffuser d(0.8);
    scrt::math::Rng rng(13);

    scrt::core::Ray r;
    r.origin    = {0.0, 0.0, 1.0};
    r.direction = {0.0, 0.0, -1.0};
    r.power     = 2.0;
    scrt::core::Hit h;
    h.position   = {0.0, 0.0, 0.0};
    h.normal     = {0.0, 0.0, 1.0};
    h.front_face = true;

    const int n = 200000;
    double sum_cos = 0.0;
    // Bin cos(theta) into ten equal bins; for a cosine-weighted hemisphere the probability of
    // landing in [c0, c1] is c1^2 - c0^2, so the bins are NOT equally populated and a uniform
    // sampler would fail this badly.
    std::vector<int> bins(10, 0);
    for (int i = 0; i < n; ++i) {
        const auto ia = d.interact(r, h, rng);
        REQUIRE(ia.kind == scrt::materials::InteractionKind::Reflected);
        const double c = glm::dot(ia.reflected.direction, h.normal);
        CHECK(c >= -1e-12);                                   // never below the surface
        CHECK(std::abs(glm::length(ia.reflected.direction) - 1.0) < 1e-12);
        CHECK(ia.reflected.power == 2.0 * 0.8);               // exact: albedo, nothing else
        sum_cos += c;
        int b = static_cast<int>(c * 10.0);
        if (b > 9) b = 9;
        if (b >= 0) ++bins[static_cast<std::size_t>(b)];
    }
    // Mean of cos over a cosine-weighted hemisphere is 2/3.
    CHECK(sum_cos / n == doctest::Approx(2.0 / 3.0).epsilon(0.01));
    for (int b = 0; b < 10; ++b) {
        const double c0 = b * 0.1, c1 = c0 + 0.1;
        const double expect = (c1 * c1 - c0 * c0) * n;
        CHECK(bins[static_cast<std::size_t>(b)] == doctest::Approx(expect).epsilon(0.06));
    }

    CHECK_THROWS_AS(scrt::materials::Diffuser(1.2), std::invalid_argument);
    CHECK_THROWS_AS(scrt::materials::Diffuser(-0.1), std::invalid_argument);
    CHECK_THROWS_AS(d.set_albedo(2.0), std::invalid_argument);
    CHECK_NOTHROW(d.set_albedo(0.04));
    CHECK(d.albedo() == 0.04);
}

namespace {

/// A 1 W pencil beam onto a diffuser plate at z = 0, with a wide receiver just above it facing
/// down. The receiver half-width is 50x its height, so the disk it subtends reaches 88.85
/// degrees and catches sin^2 = 99.96% of a Lambertian hemisphere; the square catches at least
/// that. Returns the power collected.
double scattered_up_w(double albedo) {
    scrt::scene::Scene scene;
    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* abs_ptr = absorber.get();
    scene.add_material(std::move(absorber));
    auto diff = std::make_unique<scrt::materials::Diffuser>(albedo);
    auto* diff_ptr = diff.get();
    scene.add_material(std::move(diff));

    auto plate = std::make_unique<scrt::surfaces::Plane>(0.05, 0.05);
    plate->set_material(diff_ptr);
    scene.add_surface(std::move(plate));

    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 8, 8);
    recv->surface()->set_material(abs_ptr);
    recv->set_transform(scrt::core::Transform::from_translation({0.0, 0.0, 0.01}));
    scene.set_receiver(std::move(recv));

    // The laser sits in the 10 mm gap between the diffuser and the receiver, pointing down, so
    // nothing blocks the incoming beam and everything scattered upward is caught.
    auto laser = std::make_unique<scrt::sources::Laser>();
    laser->set_origin({0.0, 0.0, 0.005});
    laser->set_direction({0.0, 0.0, -1.0});
    laser->set_power_w(1.0);
    laser->set_beam_diameter_m(0.0);
    scene.add_source(std::move(laser));
    scene.build_acceleration_structure();

    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = 100000;
    cfg.rng_seed       = 19;
    cfg.num_threads    = 1;
    cfg.max_bounces    = 4;
    scrt::tracer::Tracer tracer(scene);
    tracer.run(cfg);
    return scene.receiver()->accumulator().total_power_w();
}

} // namespace

TEST_CASE("Diffuser: the albedo is the fraction that comes back, end to end") {
    // A white screen returns 80% of a 1 W beam into the hemisphere above it.
    CHECK(scattered_up_w(0.8) == doctest::Approx(0.8).epsilon(0.005));
    // Matte black paint: gap G1's realistic absorber, 4% rather than the ideal absorber's 0%.
    CHECK(scattered_up_w(0.04) == doctest::Approx(0.04).epsilon(0.01));
    // Albedo 0 is an absorber that happens to be spelt as a diffuser.
    CHECK(scattered_up_w(0.0) == 0.0);
}

TEST_CASE("Diffuser: builds from the document and survives a strict round trip") {
    scrt::io::MaterialDoc md;
    md.id     = "white";
    md.type   = "diffuser";
    md.params = nlohmann::json{{"albedo", 0.85}};
    auto m = scrt::io::build_material(md);
    auto* d = dynamic_cast<scrt::materials::Diffuser*>(m.get());
    REQUIRE(d);
    CHECK(d->albedo() == 0.85);

    nlohmann::json doc = nlohmann::json::parse(R"({"scene": {
      "materials": [ {"id":"white","type":"diffuser","albedo":0.85} ],
      "elements": [ {"name":"screen","material":"white",
                     "surface": {"type":"plane","half_width":0.05,"half_height":0.05}} ],
      "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                    "grid": {"nx":8,"ny":8} },
      "sources": [] }})");
    const auto parsed = scrt::io::parse_document(doc, /*strict=*/true);
    const auto w = scrt::io::write_document(parsed);
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);
    const auto ls = scrt::io::build_scene(parsed, std::filesystem::path(SCRT_SOURCE_DIR));
    CHECK(dynamic_cast<const scrt::materials::Diffuser*>(ls.scene->surfaces()[0]->material()));

    nlohmann::json bad = doc;
    bad["scene"]["materials"][0]["albedo"] = 1.5;
    CHECK_THROWS_AS(scrt::io::build_scene(scrt::io::parse_document(bad, true),
                                          std::filesystem::path(SCRT_SOURCE_DIR)),
                    std::invalid_argument);
    nlohmann::json stray = doc;
    stray["scene"]["materials"][0]["reflectance"] = 0.5;   // the key is albedo, not reflectance
    CHECK_THROWS_AS(scrt::io::parse_document(stray, true), std::runtime_error);
}
