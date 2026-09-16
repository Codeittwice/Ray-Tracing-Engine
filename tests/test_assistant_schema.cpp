// The scene assistant's system prompt (src/viz/panels/AiOverlay.cpp) tells the model exactly
// which keys and surface types exist, and the reply is parsed in STRICT mode, where an
// unrecognised key is an error rather than a shrug.
//
// That makes the prompt a contract with io::parse_document, and a contract nothing else checks:
// the assistant's failure mode for a wrong prompt is "every design it produces fails to load",
// which nobody would see until a user with an API key tried it. These tests hold the two in
// step by parsing, in strict mode, exactly what the prompt promises.
//
// If a test here fails after a change to SceneDocument, the fix is usually in the PROMPT, not in
// the test - the prompt is what has gone stale.

#include "scrt/io/SceneDocument.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace {

/// The document shape the prompt shows the model, verbatim apart from its explanatory comments.
const char* const kPromptExample = R"JSON(
{
  "scene": {
    "name": "Panel cooker",
    "sun": {
      "direction": [0.0, 0.0, -1.0],
      "dni_wm2": 1000.0,
      "sunshape": {"type": "pillbox", "half_angle_mrad": 4.65}
    },
    "aperture": {
      "type": "disk",
      "center": [0.0, 0.0, 1.0],
      "normal": [0.0, 0.0, 1.0],
      "radius": 0.8
    },
    "materials": [
      {"id": "foil_mirror", "type": "real_mirror", "reflectance": 0.85, "slope_error_mrad": 4.0},
      {"id": "pot", "type": "absorber"}
    ],
    "elements": [
      {
        "name": "back_panel",
        "material": "foil_mirror",
        "surface": {"type": "plane", "half_width": 0.35, "half_height": 0.25},
        "transform": {"rotation_euler_deg": [67.5, 0.0, 0.0], "translation": [0.0, 0.35, 0.35]}
      }
    ],
    "receiver": {
      "surface": {"type": "plane", "half_width": 0.1, "half_height": 0.1},
      "grid": {"nx": 32, "ny": 32},
      "transform": {"translation": [0.0, 0.0, 0.0]}
    }
  },
  "trace": {
    "n_primary_rays": 100000, "max_bounces": 3,
    "record_paths": false, "max_paths_to_record": 200, "rng_seed": 13
  }
}
)JSON";

/// Wraps one element surface in the smallest document that still parses, so a surface type can
/// be checked on its own.
json document_with_surface(const json& surface) {
    json doc = json::parse(kPromptExample);
    doc["scene"]["elements"][0]["surface"] = surface;
    return doc;
}

} // namespace

TEST_CASE("assistant prompt: the example document it shows the model parses in strict mode") {
    const json j = json::parse(kPromptExample);
    scrt::io::SceneDocument doc;
    REQUIRE_NOTHROW(doc = scrt::io::parse_document(j, /*strict=*/true));

    CHECK(doc.name == "Panel cooker");
    CHECK(doc.elements.size() == 1);
    CHECK(doc.materials.size() == 2);
    REQUIRE(scrt::io::first_sun(doc) != nullptr);
    CHECK(scrt::io::first_sun(doc)->dni_wm2 == doctest::Approx(1000.0));
}

TEST_CASE("assistant prompt: every surface type it offers is a real one") {
    // Each entry is exactly the key set the prompt lists for that type. A type the prompt spells
    // differently, or a key it invents, fails here.
    SUBCASE("plane") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "plane"}, {"half_width", 0.35}, {"half_height", 0.25}}),
            true));
    }
    SUBCASE("sphere") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "sphere"}, {"radius", 0.2}}), true));
    }
    SUBCASE("disk") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "disk"}, {"radius", 0.0127}, {"hole_radius", 0.002}}),
            true));
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "disk"}, {"radius", 0.0254}}), true));
    }
    SUBCASE("thick_lens") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "thick_lens"}, {"radius1", 0.0258}, {"radius2", 0.0},
                                   {"center_thickness_m", 0.0053}, {"diameter_m", 0.0254}}),
            true));
    }
    SUBCASE("slit_plate") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "slit_plate"}, {"half_width", 0.02},
                                   {"half_height", 0.02}, {"slit_width", 0.0001},
                                   {"slit_count", 2}, {"slit_pitch", 0.0005}}),
            true));
    }
    SUBCASE("paraboloid") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "paraboloid"},
                                   {"focal_length_m", 0.6},
                                   {"aperture_radius_m", 0.75}}),
            true));
    }
    SUBCASE("cylindrical_paraboloid") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "cylindrical_paraboloid"},
                                   {"focal_length_m", 0.4},
                                   {"aperture_half_width_m", 0.5},
                                   {"aperture_half_length_m", 1.0}}),
            true));
    }
    SUBCASE("fresnel_zone_lens") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "fresnel_zone_lens"},
                                   {"focal_length_m", 0.5},
                                   {"inner_radius_m", 0.02},
                                   {"pitch_m", 0.001},
                                   {"n_zones", 200},
                                   {"n_lens", 1.49}}),
            true));
    }
    SUBCASE("quadric") {
        CHECK_NOTHROW(scrt::io::parse_document(
            document_with_surface({{"type", "quadric"},
                                   {"coeffs", {{"A", 1.0}, {"B", 1.0}, {"I", -1.0}}},
                                   {"aperture_box",
                                    {{"min", {-1.0, -1.0, -1.0}}, {"max", {1.0, 1.0, 1.0}}}}}),
            true));
    }
}

TEST_CASE("assistant prompt: every material type it offers is a real one") {
    const json materials = json::array({
        {{"id", "m0"}, {"type", "perfect_mirror"}},
        {{"id", "m1"}, {"type", "real_mirror"}, {"reflectance", 0.9}, {"slope_error_mrad", 3.0}},
        {{"id", "m2"}, {"type", "absorber"}},
        {{"id", "m3"}, {"type", "dielectric"}, {"n", 1.5}, {"absorption_per_m", 0.1}},
        {{"id", "m4"}, {"type", "thin_dielectric_pane"}, {"n", 1.5}, {"thickness_m", 0.004},
         {"absorption_per_m", 0.0}},
    });

    json doc = json::parse(kPromptExample);
    doc["scene"]["materials"]                  = materials;
    doc["scene"]["elements"][0]["material"]    = "m1";

    scrt::io::SceneDocument parsed;
    REQUIRE_NOTHROW(parsed = scrt::io::parse_document(doc, /*strict=*/true));
    CHECK(parsed.materials.size() == 5);
}

TEST_CASE("assistant prompt: diffuser takes exactly albedo") {
    json doc = json::parse(kPromptExample);
    doc["scene"]["materials"].push_back({{"id", "white"}, {"type", "diffuser"}, {"albedo", 0.8}});
    CHECK_NOTHROW(scrt::io::parse_document(doc, true));
    doc["scene"]["materials"].back()["reflectance"] = 0.8;
    CHECK_THROWS(scrt::io::parse_document(doc, true));
}

TEST_CASE("assistant prompt: beam_splitter takes exactly reflectance and absorptance") {
    json doc = json::parse(kPromptExample);
    doc["scene"]["materials"].push_back(
        {{"id", "bs"}, {"type", "beam_splitter"}, {"reflectance", 0.5}, {"absorptance", 0.02}});
    CHECK_NOTHROW(scrt::io::parse_document(doc, true));
    doc["scene"]["materials"].back()["n"] = 1.5;
    CHECK_THROWS(scrt::io::parse_document(doc, true));
}

TEST_CASE("assistant prompt: a transform may carry translation, rotation and scale together") {
    json doc = json::parse(kPromptExample);
    doc["scene"]["elements"][0]["transform"] = {{"translation", {0.1, 0.2, 0.3}},
                                                {"rotation_euler_deg", {10.0, 20.0, 30.0}},
                                                {"scale", {1.0, 2.0, 1.0}}};
    CHECK_NOTHROW(scrt::io::parse_document(doc, /*strict=*/true));
}

TEST_CASE("assistant prompt: body takes exactly substrate_m, cube and post") {
    json doc = json::parse(kPromptExample);
    doc["scene"]["elements"][0]["body"] = {{"substrate_m", 0.006}, {"post", true}};
    CHECK_NOTHROW(scrt::io::parse_document(doc, true));
    doc["scene"]["elements"][0]["body"] = {{"cube", true}, {"post", true}};
    CHECK_NOTHROW(scrt::io::parse_document(doc, true));
    doc["scene"]["elements"][0]["body"] = {{"cube", true}, {"substrate_m", 0.006}};
    CHECK_THROWS(scrt::io::parse_document(doc, true));
    doc["scene"]["elements"][0]["body"] = {{"thickness_m", 0.006}};
    CHECK_THROWS(scrt::io::parse_document(doc, true));
}

TEST_CASE("assistant prompt: strict mode is what catches an invented key") {
    // The reason the assistant parses strictly at all. Without this, a hallucinated field lands
    // in the viewport as a scene that looks plausible and quietly ignores what was asked for.
    json doc = json::parse(kPromptExample);
    doc["scene"]["elements"][0]["surface"]["thickness_mm"] = 3.0;
    CHECK_THROWS(scrt::io::parse_document(doc, /*strict=*/true));
}
