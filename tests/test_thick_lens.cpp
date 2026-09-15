#include <doctest/doctest.h>
#include "scrt/core/Hit.hpp"
#include "scrt/core/Ray.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/surfaces/ThickLens.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cmath>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <vector>

// Wave 3 Stage 4: a real lens as one closed solid, and the Sellmeier presets it needs (G2).

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using scrt::math::vec3;

namespace {

scrt::core::Ray along_z(double x, double y, double z0 = -1.0) {
    scrt::core::Ray r;
    r.origin    = {x, y, z0};
    r.direction = {0.0, 0.0, 1.0};
    return r;
}

} // namespace

TEST_CASE("ThickLens: faces, rim and validation") {
    // Plano-convex, curved face toward -z: R1 = 25.8 mm, flat back, 5.3 mm thick at the centre
    // (a 25.8 mm radius sags 3.3 mm over a 25.4 mm aperture, so 3 mm would be impossible; the
    // catalogue f = 50 mm lens is 5.3 mm), 1 inch.
    scrt::surfaces::ThickLens pcx(0.0258, 0.0, 0.0053, 0.0254);
    CHECK(pcx.z_front(0.0) == doctest::Approx(-0.00265));
    CHECK(pcx.z_back(0.0) == 0.00265);
    CHECK(pcx.z_front(0.0127) > pcx.z_front(0.0));   // the curved face sags back toward +z
    CHECK(pcx.edge_thickness() > 0.0);
    CHECK(pcx.edge_thickness() < 0.0053);

    scrt::core::Hit h;
    // On axis from -z: enters at the front vertex, entering the body, normal facing -z.
    REQUIRE(pcx.intersect(along_z(0.0, 0.0), 1e-9, 1e9, h));
    CHECK(h.position.z == doctest::Approx(-0.00265));
    CHECK(h.front_face);
    CHECK(h.normal.z == doctest::Approx(-1.0));
    // Continue from just inside: the next hit is the flat back face, leaving the body.
    scrt::core::Ray inside = along_z(0.0, 0.0, h.position.z + 1e-6);
    REQUIRE(pcx.intersect(inside, 1e-9, 1e9, h));
    CHECK(h.position.z == doctest::Approx(0.00265));
    CHECK_FALSE(h.front_face);
    // Off axis inside the aperture: hits the curved face at its sag; outside: misses.
    REQUIRE(pcx.intersect(along_z(0.010, 0.0), 1e-9, 1e9, h));
    CHECK(h.position.z == doctest::Approx(pcx.z_front(0.010)));
    CHECK_FALSE(pcx.intersect(along_z(0.013, 0.0), 1e-9, 1e9, h));
    // A ray across the rim, radially inward, at a height where the rim exists: the curved face
    // has sagged past z = 0 by the edge (z_front(12.7 mm) = +0.7 mm), so probe at z = 2 mm,
    // between that and the flat back at +2.65 mm.
    scrt::core::Ray side;
    side.origin = {0.05, 0.0, 0.002};
    side.direction = {-1.0, 0.0, 0.0};
    REQUIRE(pcx.intersect(side, 1e-9, 1e9, h));
    CHECK(h.position.x == doctest::Approx(0.0127));
    CHECK(h.normal.x == doctest::Approx(1.0));
    CHECK(h.front_face);

    // Bi-concave: the body is outside both spheres; on axis the faces are at -t/2 and +t/2.
    scrt::surfaces::ThickLens dcv(-0.05, 0.05, 0.002, 0.0254);
    REQUIRE(dcv.intersect(along_z(0.0, 0.0), 1e-9, 1e9, h));
    CHECK(h.position.z == doctest::Approx(-0.001));
    CHECK(h.front_face);
    CHECK(dcv.edge_thickness() > 0.002);

    CHECK_THROWS_AS(scrt::surfaces::ThickLens(0.005, 0.0, 0.003, 0.0254), std::invalid_argument);   // R < D/2
    CHECK_THROWS_AS(scrt::surfaces::ThickLens(0.0127, -0.0127, 0.001, 0.0254), std::invalid_argument); // faces cross
    CHECK_THROWS_AS(scrt::surfaces::ThickLens(0.05, 0.0, 0.0, 0.0254), std::invalid_argument);
}

TEST_CASE("Sellmeier presets: the new glasses read their catalogue indices at the d line") {
    scrt::materials::Dielectric g(1.5, 0.0);
    g.set_sellmeier(scrt::materials::SellmeierCoeffs::n_sf11());
    CHECK(g.n_at(587.56) == doctest::Approx(1.7847).epsilon(0.001));
    g.set_sellmeier(scrt::materials::SellmeierCoeffs::pmma());
    CHECK(g.n_at(587.56) == doctest::Approx(1.491).epsilon(0.003));
    g.set_sellmeier(scrt::materials::SellmeierCoeffs::polycarbonate());
    CHECK(g.n_at(587.56) == doctest::Approx(1.585).epsilon(0.002));
    // Normal dispersion in every preset: blue bends more than red.
    for (const char* name : {"bk7", "fused_silica", "n_sf11", "pmma", "polycarbonate"}) {
        auto c = scrt::materials::SellmeierCoeffs::by_name(name);
        REQUIRE(c.has_value());
        g.set_sellmeier(*c);
        CHECK(g.n_at(450.0) > g.n_at(650.0));
    }
    CHECK_FALSE(scrt::materials::SellmeierCoeffs::by_name("diamond").has_value());
}

namespace {

/// Where a collimated pencil ray at height h, travelling +z through a BK7 plano-convex lens
/// (curved face first), crosses the axis after the lens, measured from the back vertex.
/// Read from the recorded path by following the highest-power branch out of each node.
double back_focal_distance_m(double h, double wavelength_nm, double& n_out) {
    scrt::scene::Scene scene;
    auto absorber = std::make_unique<scrt::materials::Absorber>();
    auto* abs_ptr = absorber.get();
    scene.add_material(std::move(absorber));
    auto glass = std::make_unique<scrt::materials::Dielectric>(1.5, 0.0);
    glass->set_sellmeier(scrt::materials::SellmeierCoeffs::bk7());
    n_out = glass->n_at(wavelength_nm);
    auto* glass_ptr = glass.get();
    scene.add_material(std::move(glass));

    const double R = 0.0258, t = 0.0053;
    auto lens = std::make_unique<scrt::surfaces::ThickLens>(R, 0.0, t, 0.0254);
    lens->set_material(glass_ptr);
    scene.add_surface(std::move(lens));

    // A screen well past the focus, facing -z, so the exiting ray lands somewhere.
    auto recv = std::make_unique<scrt::scene::Receiver>(0.05, 0.05, 4, 4);
    recv->surface()->set_material(abs_ptr);
    recv->set_transform(scrt::core::Transform::from_translation({0.0, 0.0, 0.2}));
    scene.set_receiver(std::move(recv));

    auto laser = std::make_unique<scrt::sources::Laser>();
    laser->set_origin({h, 0.0, -0.1});
    laser->set_direction({0.0, 0.0, 1.0});
    laser->set_power_w(1.0);
    laser->set_wavelength_nm(wavelength_nm);
    laser->set_beam_diameter_m(0.0);
    scene.add_source(std::move(laser));
    scene.build_acceleration_structure();

    scrt::tracer::TraceConfig cfg;
    cfg.n_primary_rays      = 1;
    cfg.rng_seed            = 5;
    cfg.num_threads         = 1;
    cfg.max_bounces         = 6;
    cfg.record_paths        = true;
    cfg.max_paths_to_record = 1;
    scrt::tracer::Tracer tracer(scene);
    const auto res = tracer.run(cfg);
    REQUIRE(res.sampled_paths.size() == 1);
    const auto& path = res.sampled_paths[0];

    // Walk the trunk: from node 0 take the outgoing edge with the most power, repeatedly.
    std::vector<std::uint32_t> trunk{0};
    for (;;) {
        std::uint32_t cur = trunk.back();
        double best_p = -1.0;
        std::uint32_t next = cur;
        for (std::size_t e = 0; e < path.edges.size(); ++e)
            if (path.edges[e][0] == cur && path.edge_power_w[e] > best_p) {
                best_p = path.edge_power_w[e];
                next   = path.edges[e][1];
            }
        if (next == cur) break;
        trunk.push_back(next);
    }
    // origin -> front face -> back face -> screen
    REQUIRE(trunk.size() == 4);
    const vec3 exit = path.nodes[trunk[2]];
    const vec3 land = path.nodes[trunk[3]];
    CHECK(exit.z == doctest::Approx(0.5 * t));
    // Cross the axis: x(z) = exit.x + (z - exit.z) * (land.x - exit.x) / (land.z - exit.z) = 0.
    const double z_cross = exit.z - exit.x * (land.z - exit.z) / (land.x - exit.x);
    return z_cross - 0.5 * t;
}

} // namespace

TEST_CASE("ThickLens: a BK7 plano-convex lens focuses where the thick-lens formula says, at two colours") {
    // Curved face toward the collimated beam, flat back: f = R / (n - 1), and the back focal
    // distance from the flat vertex is f - t / n. Paraxial at h = 1 mm on a 25.8 mm radius, so
    // spherical aberration is well under the 0.5% tolerance.
    const double R = 0.0258, t = 0.0053;
    double n550 = 0.0, n1064 = 0.0;
    const double bfd_550  = back_focal_distance_m(0.001, 550.0, n550);
    const double bfd_1064 = back_focal_distance_m(0.001, 1064.0, n1064);
    const double expect_550  = R / (n550 - 1.0) - t / n550;
    const double expect_1064 = R / (n1064 - 1.0) - t / n1064;
    CHECK(bfd_550 == doctest::Approx(expect_550).epsilon(0.005));
    CHECK(bfd_1064 == doctest::Approx(expect_1064).epsilon(0.005));
    CHECK(bfd_1064 > bfd_550);   // longer focus in the infrared: the lens disperses
}

TEST_CASE("ThickLens: round-trips through the document strictly and builds") {
    nlohmann::json doc = nlohmann::json::parse(R"({"scene": {
      "materials": [ {"id":"bk7","type":"dielectric","n":1.5168,"sellmeier":"bk7"},
                     {"id":"flint","type":"dielectric","n":1.7847,"sellmeier":"n_sf11"} ],
      "elements": [ {"name":"pcx","material":"bk7",
                     "surface": {"type":"thick_lens","radius1":0.0258,"radius2":0.0,
                                 "center_thickness_m":0.0053,"diameter_m":0.0254}} ],
      "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                    "grid": {"nx":8,"ny":8} },
      "sources": [] }})");
    const auto parsed = scrt::io::parse_document(doc, /*strict=*/true);
    const auto w = scrt::io::write_document(parsed);
    CHECK(w["scene"]["elements"][0]["surface"]["type"] == "thick_lens");
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);
    const auto ls = scrt::io::build_scene(parsed, std::filesystem::path(SCRT_SOURCE_DIR));
    CHECK(dynamic_cast<const scrt::surfaces::ThickLens*>(ls.scene->surfaces()[0].get()));

    nlohmann::json bad = doc;
    bad["scene"]["elements"][0]["surface"]["radius1"] = 0.005;   // smaller than the half-diameter
    CHECK_THROWS_AS(scrt::io::parse_document(bad, true), std::runtime_error);
    nlohmann::json glass = doc;
    glass["scene"]["materials"][1]["sellmeier"] = "diamond";
    CHECK_THROWS_AS(scrt::io::parse_document(glass, true), std::runtime_error);
}
