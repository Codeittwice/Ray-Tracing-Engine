#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/BeamSplitter.hpp"
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

// Wave 3 Stage 3: a designed split ratio, held at every angle.

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using scrt::math::vec3;

TEST_CASE("BeamSplitter: designed ratio at every angle, R + T + A = 1 exactly") {
    scrt::materials::BeamSplitter bs(0.3, 0.1);
    CHECK(bs.transmittance() == 1.0 - 0.3 - 0.1);
    scrt::math::Rng rng(1);

    for (double deg : {0.0, 20.0, 45.0, 70.0, 89.0}) {
        const double th = deg * scrt::math::DEG2RAD;
        scrt::core::Ray r;
        r.origin    = {0.0, 0.0, 1.0};
        r.direction = glm::normalize(vec3{std::sin(th), 0.0, -std::cos(th)});
        r.power     = 2.0;
        scrt::core::Hit h;
        h.position   = {0.0, 0.0, 0.0};
        h.normal     = {0.0, 0.0, 1.0};
        h.front_face = true;

        const auto ia = bs.interact(r, h, rng);
        REQUIRE(ia.kind == scrt::materials::InteractionKind::Split);
        CHECK(ia.reflected.power == 2.0 * 0.3);            // exact, not Fresnel
        CHECK(ia.transmitted.power == 2.0 * (1.0 - 0.3 - 0.1));
        CHECK(ia.transmitted.direction == r.direction);     // straight through
        CHECK(ia.reflected.direction.z == doctest::Approx(std::cos(th)));   // mirrored
        CHECK(ia.reflected.direction.x == doctest::Approx(std::sin(th)));
    }

    CHECK_THROWS_AS(scrt::materials::BeamSplitter(0.6, 0.5), std::invalid_argument);
    CHECK_THROWS_AS(scrt::materials::BeamSplitter(-0.1), std::invalid_argument);
    CHECK_THROWS_AS(bs.set_reflectance(0.95), std::invalid_argument);   // 0.95 + 0.1 > 1
    CHECK_NOTHROW(bs.set_reflectance(0.5));
    CHECK(bs.reflectance() == 0.5);
}

namespace {

/// A 5 W pencil laser pointing down onto a splitter plate tilted 45 degrees about y at the
/// origin. `catch_reflected` puts the screen where the reflected beam goes (along +x or -x,
/// found from the trace), otherwise straight below.
double screen_power(double R, double A, bool catch_reflected) {
    scrt::scene::Scene scene;
    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* abs_ptr = absorber.get();
    scene.add_material(std::move(absorber));
    auto bs = std::make_unique<scrt::materials::BeamSplitter>(R, A);
    auto* bs_ptr = bs.get();
    scene.add_material(std::move(bs));

    auto plate = std::make_unique<scrt::surfaces::Plane>(0.1, 0.1);
    plate->set_material(bs_ptr);
    plate->set_transform(scrt::core::Transform::from_rotation_axis_angle({0.0, 1.0, 0.0},
                                                                         45.0 * scrt::math::DEG2RAD));
    scene.add_surface(std::move(plate));

    auto recv = std::make_unique<scrt::scene::Receiver>(0.2, 0.2, 4, 4);
    recv->surface()->set_material(abs_ptr);
    if (catch_reflected) {
        // The plate normal is (sin45, 0, cos45); a downward beam reflects to exactly +x, so a
        // vertical screen at x = 1 facing -x catches all of it.
        recv->set_transform(scrt::core::Transform::from_trs(
            {1.0, 0.0, 0.0}, {0.0, 90.0 * scrt::math::DEG2RAD, 0.0}, {1.0, 1.0, 1.0}));
    } else {
        recv->set_transform(scrt::core::Transform::from_translation({0.0, 0.0, -1.0}));
    }
    scene.set_receiver(std::move(recv));

    auto laser = std::make_unique<scrt::sources::Laser>();
    laser->set_origin({0.0, 0.0, 1.0});
    laser->set_direction({0.0, 0.0, -1.0});
    laser->set_power_w(5.0);
    laser->set_beam_diameter_m(0.0);
    scene.add_source(std::move(laser));
    scene.build_acceleration_structure();

    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = 1000;
    cfg.rng_seed       = 3;
    cfg.num_threads    = 1;
    cfg.max_bounces    = 4;
    scrt::tracer::Tracer tracer(scene);
    tracer.run(cfg);
    return scene.receiver()->accumulator().total_power_w();
}

} // namespace

TEST_CASE("BeamSplitter: a 50:50 plate at 45 degrees sends half a laser each way") {
    // Straight through: exactly half the watts land on the screen below (N rays of 2.5/N).
    CHECK(screen_power(0.5, 0.0, false) == doctest::Approx(2.5).epsilon(1e-12));
    // Sideways: the reflected half lands on the vertical screen at +x.
    CHECK(screen_power(0.5, 0.0, true) == doctest::Approx(2.5).epsilon(1e-12));
    // Lossy 30:10:60 splitter: the transmitted screen sees 60%.
    CHECK(screen_power(0.3, 0.1, false) == doctest::Approx(3.0).epsilon(1e-12));
}

TEST_CASE("BeamSplitter: builds from the document and survives a strict round trip") {
    scrt::io::MaterialDoc md;
    md.id     = "bs";
    md.type   = "beam_splitter";
    md.params = nlohmann::json{{"reflectance", 0.5}, {"absorptance", 0.02}};
    auto m = scrt::io::build_material(md);
    auto* bs = dynamic_cast<scrt::materials::BeamSplitter*>(m.get());
    REQUIRE(bs);
    CHECK(bs->reflectance() == 0.5);
    CHECK(bs->absorptance() == 0.02);

    nlohmann::json doc = nlohmann::json::parse(R"({"scene": {
      "materials": [ {"id":"bs","type":"beam_splitter","reflectance":0.5,"absorptance":0.02} ],
      "elements": [ {"name":"plate","material":"bs",
                     "surface": {"type":"plane","half_width":0.05,"half_height":0.05}} ],
      "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                    "grid": {"nx":8,"ny":8} },
      "sources": [] }})");
    const auto parsed = scrt::io::parse_document(doc, /*strict=*/true);
    const auto w = scrt::io::write_document(parsed);
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);
    const auto ls = scrt::io::build_scene(parsed, std::filesystem::path(SCRT_SOURCE_DIR));
    CHECK(dynamic_cast<const scrt::materials::BeamSplitter*>(ls.scene->surfaces()[0]->material()));

    nlohmann::json bad = doc;
    bad["scene"]["materials"][0]["reflectance"] = 0.99;   // R + A > 1
    CHECK_THROWS_AS(scrt::io::build_scene(scrt::io::parse_document(bad, true),
                                          std::filesystem::path(SCRT_SOURCE_DIR)),
                    std::invalid_argument);
    nlohmann::json stray = doc;
    stray["scene"]["materials"][0]["n"] = 1.5;
    CHECK_THROWS_AS(scrt::io::parse_document(stray, true), std::runtime_error);
}
