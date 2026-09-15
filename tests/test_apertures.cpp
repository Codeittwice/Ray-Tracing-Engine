#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/surfaces/Disk.hpp"
#include "scrt/surfaces/SlitPlate.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cmath>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <variant>

// Wave 3 Stage 2, gap G3: a circular (optionally holed) plate and a slotted plate. New surface
// types rather than a clip on Plane, so the rectangle every golden scene traces is untouched.

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using scrt::math::vec3;
using json = nlohmann::json;

namespace {

scrt::core::Ray down_at(double x, double y) {
    scrt::core::Ray r;
    r.origin    = {x, y, 1.0};
    r.direction = {0.0, 0.0, -1.0};
    return r;
}

bool hits(const scrt::surfaces::Surface& s, const scrt::core::Ray& r) {
    scrt::core::Hit h;
    return s.intersect(r, 1e-9, 1e9, h);
}

} // namespace

TEST_CASE("Disk: hits inside the rim, misses outside it and inside the hole") {
    scrt::surfaces::Disk solid(0.5);
    CHECK(hits(solid, down_at(0.0, 0.0)));
    CHECK(hits(solid, down_at(0.49, 0.0)));
    CHECK(hits(solid, down_at(0.3, 0.3)));           // r = 0.424 < 0.5
    CHECK_FALSE(hits(solid, down_at(0.4, 0.4)));     // r = 0.566: inside the bounding square, outside the disk
    CHECK_FALSE(hits(solid, down_at(0.51, 0.0)));

    scrt::surfaces::Disk iris(0.5, 0.1);
    CHECK_FALSE(hits(iris, down_at(0.0, 0.0)));      // through the hole
    CHECK_FALSE(hits(iris, down_at(0.05, 0.05)));    // r = 0.0707 < 0.1
    CHECK(hits(iris, down_at(0.11, 0.0)));           // on the ring
    CHECK(hits(iris, down_at(0.49, 0.0)));

    scrt::core::Hit h;
    REQUIRE(iris.intersect(down_at(0.2, 0.1), 1e-9, 1e9, h));
    CHECK(h.front_face);
    CHECK(h.normal.z == 1.0);
    CHECK(h.uv.x == doctest::Approx(0.2));
    CHECK(h.uv.y == doctest::Approx(0.1));
    CHECK(h.position.z == doctest::Approx(0.0));

    CHECK_THROWS_AS(scrt::surfaces::Disk(0.0), std::invalid_argument);
    CHECK_THROWS_AS(scrt::surfaces::Disk(0.5, 0.5), std::invalid_argument);
    CHECK_THROWS_AS(scrt::surfaces::Disk(0.5, -0.1), std::invalid_argument);
}

TEST_CASE("SlitPlate: hits the plate, misses the slots, single and double") {
    scrt::surfaces::SlitPlate single(0.5, 0.5, 0.1, 1, 0.0);
    CHECK_FALSE(hits(single, down_at(0.0, 0.0)));    // in the slot
    CHECK_FALSE(hits(single, down_at(0.049, 0.4)));
    CHECK(hits(single, down_at(0.051, 0.0)));        // just past the slot edge
    CHECK(hits(single, down_at(-0.3, 0.0)));
    CHECK_FALSE(hits(single, down_at(0.6, 0.0)));    // off the plate entirely

    scrt::surfaces::SlitPlate dbl(0.5, 0.5, 0.05, 2, 0.3);   // slots centred on x = -0.15, +0.15
    CHECK_FALSE(hits(dbl, down_at(-0.15, 0.0)));
    CHECK_FALSE(hits(dbl, down_at(0.15, 0.0)));
    CHECK_FALSE(hits(dbl, down_at(0.17, 0.0)));      // 0.02 from centre, half width 0.025
    CHECK(hits(dbl, down_at(0.0, 0.0)));             // between the slots
    CHECK(hits(dbl, down_at(0.19, 0.0)));            // past the slot edge
    CHECK(hits(dbl, down_at(-0.4, 0.0)));
    CHECK(dbl.in_slot(0.15));
    CHECK_FALSE(dbl.in_slot(0.0));

    CHECK_THROWS_AS(scrt::surfaces::SlitPlate(0.5, 0.5, 0.1, 0, 0.3), std::invalid_argument);
    CHECK_THROWS_AS(scrt::surfaces::SlitPlate(0.5, 0.5, 0.1, 2, 0.1), std::invalid_argument);  // merge
    CHECK_THROWS_AS(scrt::surfaces::SlitPlate(0.5, 0.5, -0.1, 1, 0.0), std::invalid_argument);
}

TEST_CASE("Disk and SlitPlate tessellate to triangles inside their own bounds") {
    auto check = [](const scrt::surfaces::Surface& s) {
        std::vector<vec3> v;
        std::vector<std::uint32_t> idx;
        s.tessellate(16, v, idx);
        REQUIRE(!v.empty());
        REQUIRE(idx.size() % 3 == 0);
        const auto b = s.local_bounds();
        for (const auto& p : v) {
            CHECK(p.x >= b.min().x - 1e-12); CHECK(p.x <= b.max().x + 1e-12);
            CHECK(p.y >= b.min().y - 1e-12); CHECK(p.y <= b.max().y + 1e-12);
            CHECK(std::abs(p.z) < 1e-12);
        }
        for (auto i : idx) CHECK(i < v.size());
    };
    check(scrt::surfaces::Disk(0.3));
    check(scrt::surfaces::Disk(0.3, 0.1));
    check(scrt::surfaces::SlitPlate(0.5, 0.2, 0.05, 3, 0.2));
}

namespace {

/// Collimated overhead sun through an aperture disk of radius 0.5 (inscribed in a 1 m plate
/// at z = 0), with `block` between them at z = 0.5. Returns the fraction of the sun's power
/// that reaches the plate.
double fraction_through(std::unique_ptr<scrt::surfaces::Surface> block, std::size_t n_rays) {
    scrt::scene::Scene scene;
    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* abs_ptr = absorber.get();
    scene.add_material(std::move(absorber));

    block->set_material(abs_ptr);
    block->set_transform(scrt::core::Transform::from_translation({0.0, 0.0, 0.5}));
    scene.add_surface(std::move(block));

    auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 8, 8);
    recv->surface()->set_material(abs_ptr);
    scene.set_receiver(std::move(recv));

    auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
    sun->set_sun_direction({0.0, 0.0, -1.0});
    sun->set_dni(1000.0);
    scrt::scene::Aperture ap;
    ap.center = {0.0, 0.0, 1.0};
    ap.radius = 0.5;
    sun->set_aperture(ap);
    const double total = sun->total_power_w();
    scene.add_source(std::move(sun));
    scene.build_acceleration_structure();

    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = n_rays;
    cfg.rng_seed       = 31;
    cfg.num_threads    = 1;
    cfg.max_bounces    = 2;
    scrt::tracer::Tracer tracer(scene);
    tracer.run(cfg);
    return scene.receiver()->accumulator().total_power_w() / total;
}

} // namespace

TEST_CASE("Apertures: an absorbing disk shadows its area, an iris passes its hole, slots pass their width") {
    // Rays are uniform over the sun's disk of radius 0.5 (area pi/4), so the pass fraction is
    // an area ratio. 200k rays give a binomial standard error of about 0.1%; tolerance 1%.
    const double A = scrt::math::PI * 0.25;
    const std::size_t n = 200000;

    // Solid disk of radius 0.25 blocks pi/16 of pi/4: passes 0.75.
    CHECK(fraction_through(std::make_unique<scrt::surfaces::Disk>(0.25), n)
          == doctest::Approx(1.0 - (scrt::math::PI * 0.0625) / A).epsilon(0.01));

    // Iris: ring from 0.2 to 0.5 (covers the whole beam) with a 0.2 hole passes (0.2/0.5)^2 = 0.16.
    CHECK(fraction_through(std::make_unique<scrt::surfaces::Disk>(0.5, 0.2), n)
          == doctest::Approx(0.16).epsilon(0.02));

    // Double slit, 0.05 wide, on a plate covering the beam: the open area inside the beam disk
    // is the integral of the chord length over each slot's x range, computed numerically here.
    auto open_area = [&](double c, double w) {
        double s = 0.0;
        const int m = 2000;
        for (int i = 0; i < m; ++i) {
            const double x = c - 0.5 * w + (i + 0.5) * w / m;
            const double h = 0.25 - x * x;
            if (h > 0.0) s += 2.0 * std::sqrt(h) * (w / m);
        }
        return s;
    };
    const double expected = (open_area(-0.15, 0.05) + open_area(0.15, 0.05)) / A;
    CHECK(fraction_through(std::make_unique<scrt::surfaces::SlitPlate>(0.6, 0.6, 0.05, 2, 0.3), n)
          == doctest::Approx(expected).epsilon(0.02));
}

TEST_CASE("Apertures: disk and slit_plate round-trip through the document and build") {
    json disk = {{"type", "disk"}, {"radius", 0.0127}, {"hole_radius", 0.002}};
    json slit = {{"type", "slit_plate"}, {"half_width", 0.02}, {"half_height", 0.02},
                 {"slit_width", 0.0001}, {"slit_count", 2}, {"slit_pitch", 0.0005}};
    json doc = json::parse(R"({"scene": {
      "materials": [ {"id":"m","type":"absorber"} ],
      "elements": [],
      "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                    "grid": {"nx":8,"ny":8} },
      "sources": [] }})");
    doc["scene"]["elements"] = json::array({
        json{{"name", "iris"}, {"material", "m"}, {"surface", disk}},
        json{{"name", "slits"}, {"material", "m"}, {"surface", slit}}});

    const auto parsed = scrt::io::parse_document(doc, /*strict=*/true);
    REQUIRE(parsed.elements.size() == 2);
    const auto* dd = std::get_if<scrt::io::DiskDoc>(&parsed.elements[0].surface);
    REQUIRE(dd);
    CHECK(dd->radius == 0.0127);
    CHECK(dd->hole_radius == 0.002);
    const auto* sd = std::get_if<scrt::io::SlitPlateDoc>(&parsed.elements[1].surface);
    REQUIRE(sd);
    CHECK(sd->slit_count == 2);
    CHECK(sd->slit_pitch == 0.0005);

    const json w = scrt::io::write_document(parsed);
    CHECK(w["scene"]["elements"][0]["surface"]["type"] == "disk");
    CHECK(w["scene"]["elements"][1]["surface"]["slit_count"] == 2);
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);

    const auto ls = scrt::io::build_scene(parsed, std::filesystem::path(SCRT_SOURCE_DIR));
    REQUIRE(ls.scene->surfaces().size() == 2);
    CHECK(dynamic_cast<const scrt::surfaces::Disk*>(ls.scene->surfaces()[0].get()));
    CHECK(dynamic_cast<const scrt::surfaces::SlitPlate*>(ls.scene->surfaces()[1].get()));

    // Strict mode still rejects an invented key, and a hole no smaller than the rim.
    json bad = disk; bad["thickness"] = 0.001;
    json d2 = doc; d2["scene"]["elements"][0]["surface"] = bad;
    CHECK_THROWS_AS(scrt::io::parse_document(d2, true), std::runtime_error);
    json bad2 = disk; bad2["hole_radius"] = 0.02;
    json d3 = doc; d3["scene"]["elements"][0]["surface"] = bad2;
    CHECK_THROWS_AS(scrt::io::parse_document(d3, true), std::runtime_error);
}

TEST_CASE("Apertures: the accumulator overload agrees with the receiver overload bit for bit") {
    // The accumulator overload (headless, scrt_compare, the GUI preview) used to deposit on ANY
    // absorbed hit, so a black iris in front of the screen was booked as light ON the screen.
    // Same rays, same receiver, two overloads: the totals must be the same double.
    auto build = [](scrt::scene::Scene& scene) {
        auto absorber = std::make_unique<scrt::materials::Absorber>();
        auto* abs_ptr = absorber.get();
        scene.add_material(std::move(absorber));
        auto iris = std::make_unique<scrt::surfaces::Disk>(0.5, 0.2);
        iris->set_material(abs_ptr);
        iris->set_transform(scrt::core::Transform::from_translation({0.0, 0.0, 0.5}));
        scene.add_surface(std::move(iris));
        auto recv = std::make_unique<scrt::scene::Receiver>(0.5, 0.5, 8, 8);
        recv->surface()->set_material(abs_ptr);
        scene.set_receiver(std::move(recv));
        auto sun = std::make_unique<scrt::sources::Pillbox>(0.0);
        sun->set_sun_direction({0.0, 0.0, -1.0});
        sun->set_dni(1000.0);
        scrt::scene::Aperture ap;
        ap.center = {0.0, 0.0, 1.0};
        ap.radius = 0.5;
        sun->set_aperture(ap);
        scene.add_source(std::move(sun));
        scene.build_acceleration_structure();
    };
    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays = 20000;
    cfg.rng_seed       = 77;
    cfg.num_threads    = 1;
    cfg.max_bounces    = 2;

    scrt::scene::Scene a;
    build(a);
    scrt::tracer::Tracer ta(a);
    ta.run(cfg);
    const double via_receiver = a.receiver()->accumulator().total_power_w();

    scrt::scene::Scene b;
    build(b);
    scrt::tracer::FluxAccumulator acc(0.5, 0.5, 8, 8);
    scrt::tracer::Tracer tb(b);
    tb.run(cfg, acc);

    CHECK(acc.total_power_w() == via_receiver);
    CHECK(via_receiver == doctest::Approx(1000.0 * scrt::math::PI * 0.04).epsilon(0.02));  // the hole
}
