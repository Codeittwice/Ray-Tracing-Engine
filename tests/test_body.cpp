#include <doctest/doctest.h>

#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/surfaces/Disk.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/viz/Body.hpp"

#include <algorithm>
#include <cmath>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

using namespace scrt;

namespace {

/// A minimal legal scene with one element whose "body" is the given JSON (null = no key).
nlohmann::json scene_with_body(const nlohmann::json& body) {
    auto j = nlohmann::json::parse(R"({
      "scene": {
        "sun": { "direction": [0,0,-1], "dni_wm2": 1000.0,
                 "sunshape": {"type":"pillbox","half_angle_mrad":4.65} },
        "aperture": { "type":"disk", "center":[0,0,2], "normal":[0,0,1], "radius":1.0 },
        "materials": [ {"id":"m","type":"perfect_mirror"} ],
        "elements": [ { "name":"mirror", "material":"m",
                        "surface": {"type":"plane","half_width":0.05,"half_height":0.03},
                        "transform": {"translation":[0,0,0.3]} } ],
        "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                      "grid": {"nx":8,"ny":8} }
      }
    })");
    if (!body.is_null()) j["scene"]["elements"][0]["body"] = body;
    return j;
}

double min_z(const std::vector<math::vec3>& v) {
    double m = 1e300;
    for (const auto& p : v) m = std::min(m, p.z);
    return m;
}

} // namespace

TEST_CASE("body: absent in a legacy element, and the writer does not invent one") {
    const auto doc = io::parse_document(scene_with_body(nullptr), true);
    REQUIRE(doc.elements.size() == 1);
    CHECK_FALSE(doc.elements[0].body.has_value());
    const auto out = io::write_document(doc);
    CHECK_FALSE(out["scene"]["elements"][0].contains("body"));
}

TEST_CASE("body: round-trips through strict parse and write exactly as written") {
    const nlohmann::json body = {{"substrate_m", 0.006}, {"post", true}};
    const auto doc = io::parse_document(scene_with_body(body), true);
    REQUIRE(doc.elements[0].body.has_value());
    CHECK(doc.elements[0].body->substrate_m == 0.006);
    CHECK(doc.elements[0].body->post);
    CHECK_FALSE(doc.elements[0].body->cube);

    const auto out = io::write_document(doc);
    CHECK(out["scene"]["elements"][0]["body"] == body);
    const auto again = io::parse_document(out, true);
    CHECK(again.elements[0].body->substrate_m == 0.006);

    const auto cube = io::parse_document(scene_with_body({{"cube", true}}), true);
    CHECK(io::write_document(cube)["scene"]["elements"][0]["body"] ==
          nlohmann::json({{"cube", true}}));
}

TEST_CASE("body: meaningless bodies are refused, and strict mode refuses unknown keys") {
    CHECK_THROWS(io::parse_document(scene_with_body({{"substrate_m", -0.001}}), true));
    CHECK_THROWS(io::parse_document(scene_with_body({{"substrate_m", -0.001}}), false));
    CHECK_THROWS(io::parse_document(scene_with_body({{"cube", true}, {"substrate_m", 0.01}}),
                                    false));
    CHECK_THROWS(io::parse_document(scene_with_body({{"mount", "kinematic"}}), true));
    CHECK_THROWS(io::parse_document(scene_with_body(nlohmann::json(42)), false));
}

TEST_CASE("body: a substrate sits BEHIND the surface, inside its outline, and turns with it") {
    surfaces::Plane plane(0.05, 0.03);
    // Tilted 30 degrees about X and lifted, so "behind" is not simply "below".
    plane.set_transform(core::Transform::from_trs({0.1, 0.2, 0.3},
                                                  {math::PI / 6, 0.0, 0.0}, {1, 1, 1}));
    io::BodyDoc body;
    body.substrate_m = 0.006;

    std::vector<math::vec3>    v;
    std::vector<std::uint32_t> idx;
    REQUIRE(viz::tessellate_body(plane, body, viz::BodyPart::Mount, v, idx));
    CHECK(v.size() == 8);
    CHECK(idx.size() % 3 == 0);
    for (auto i : idx) CHECK(i < v.size());
    for (const auto& p : v) {
        const auto q = plane.transform().point_to_local(p);
        CHECK(q.z <= 1e-12);
        CHECK(q.z >= -0.006 - 1e-12);
        CHECK(std::fabs(q.x) <= 0.05 + 1e-12);
        CHECK(std::fabs(q.y) <= 0.03 + 1e-12);
    }
    // No post was asked for.
    CHECK_FALSE(viz::tessellate_body(plane, body, viz::BodyPart::Post, v, idx));
    CHECK(v.empty());
}

TEST_CASE("body: a disk's substrate is round, not a box") {
    surfaces::Disk disk(0.0254);
    io::BodyDoc body;
    body.substrate_m = 0.005;
    std::vector<math::vec3>    v;
    std::vector<std::uint32_t> idx;
    REQUIRE(viz::tessellate_body(disk, body, viz::BodyPart::Mount, v, idx));
    for (const auto& p : v)
        CHECK(std::hypot(p.x, p.y) == doctest::Approx(0.0254).epsilon(1e-6));
}

TEST_CASE("body: a cube has the flat surface as its diagonal") {
    surfaces::Plane plate(0.02, 0.0141421356);
    io::BodyDoc body;
    body.cube = true;
    std::vector<math::vec3>    v;
    std::vector<std::uint32_t> idx;
    REQUIRE(viz::tessellate_body(plate, body, viz::BodyPart::Mount, v, idx));
    double max_x = 0, max_z = 0;
    for (const auto& p : v) {
        max_x = std::max(max_x, std::fabs(p.x));
        max_z = std::max(max_z, std::fabs(p.z));
    }
    // Half-diagonal 0.02 in both x and z, so the square's side is 0.02 * sqrt(2) = 0.0283.
    CHECK(max_x == doctest::Approx(0.02));
    CHECK(max_z == doctest::Approx(0.02));
}

TEST_CASE("body: a post reaches the floor, stays upright, and is omitted at floor level") {
    surfaces::Plane plane(0.05, 0.03);
    plane.set_transform(core::Transform::from_trs({0.1, 0.2, 0.3},
                                                  {math::PI / 2, 0.0, 0.0}, {1, 1, 1}));
    io::BodyDoc body;
    body.post = true;
    std::vector<math::vec3>    v;
    std::vector<std::uint32_t> idx;
    REQUIRE(viz::tessellate_body(plane, body, viz::BodyPart::Post, v, idx));
    CHECK(min_z(v) == 0.0);
    for (const auto& p : v) {
        CHECK(std::hypot(p.x - 0.1, p.y - 0.2) == doctest::Approx(viz::kPostRadius));
        CHECK(p.z <= 0.3);
    }

    plane.set_transform(core::Transform::from_translation({0, 0, 0}));
    CHECK_FALSE(viz::tessellate_body(plane, body, viz::BodyPart::Post, v, idx));
}
