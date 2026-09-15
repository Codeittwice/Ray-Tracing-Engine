#include <doctest/doctest.h>
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/io/SceneWriter.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/scene/SceneEditor.hpp"
#include "scrt/viz/Library.hpp"
#include <algorithm>
#include <filesystem>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <variant>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

// Wave 3 Stage 6. The library is a list of things a user can click, and the standing rule for
// this wave is that a row whose behaviour does not match its name is worse than a missing row.
// These tests are what enforce it: every entry must parse strictly, build, and be reachable.

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using json = nlohmann::json;

namespace {

/// The smallest document that parses, with `materials` replaced by one library entry.
json doc_with_material(const scrt::io::MaterialDoc& md) {
    json m = md.params.is_object() ? md.params : json::object();
    m["id"]   = md.id;
    m["type"] = md.type;
    json doc = json::parse(R"({"scene": {
      "materials": [],
      "elements": [],
      "receiver": { "surface": {"type":"plane","half_width":0.05,"half_height":0.05},
                    "grid": {"nx":8,"ny":8} },
      "sources": [] }})");
    doc["scene"]["materials"] = json::array({m});
    return doc;
}

std::filesystem::path base_dir() { return std::filesystem::path(SCRT_SOURCE_DIR) / "examples"; }

} // namespace

TEST_CASE("Library: every material entry parses strictly, builds, and has a unique name") {
    std::set<std::string> names;
    for (const auto& e : scrt::viz::builtin_materials()) {
        CAPTURE(e.name);
        CHECK_FALSE(e.name.empty());
        CHECK_FALSE(e.note.empty());       // every figure states where it comes from
        CHECK_FALSE(e.group.empty());
        CHECK(e.doc.id == e.name);         // the id the panel adds it under
        CHECK(names.insert(e.name).second);

        // Strict parse: an invented key for this material type is a hard failure at load, so a
        // library entry carrying one would break the moment the scene was saved and reopened.
        CHECK_NOTHROW(scrt::io::parse_document(doc_with_material(e.doc), /*strict=*/true));
        // And it must actually construct: an out-of-range figure throws here, not at trace time.
        CHECK_NOTHROW(scrt::io::build_material(e.doc));
    }
    CHECK(names.size() >= 25);
}

TEST_CASE("Library: every component builds and names a material the library really has") {
    std::set<std::string> names;
    for (const auto& c : scrt::viz::builtin_components()) {
        CAPTURE(c.name);
        CHECK_FALSE(c.name.empty());
        CHECK_FALSE(c.note.empty());
        CHECK(names.insert(c.name).second);

        const auto& mats = scrt::viz::builtin_materials();
        const bool  known = std::any_of(mats.begin(), mats.end(),
                                        [&](const scrt::viz::MaterialEntry& e) {
                                            return e.name == c.material;
                                        });
        // Built eagerly: doctest's CHECK_MESSAGE binds its stream operator tighter than '+',
        // so an inline concatenation does not compile (CLAUDE.md records this).
        const std::string msg =
            c.name + " names material '" + c.material + "', which is not in the library";
        CHECK_MESSAGE(known, msg);
        // The surface must build: a lens whose faces cross, or a disk with a hole bigger than
        // its rim, throws in its constructor, and the panel would report a failure on click.
        CHECK_NOTHROW(scrt::io::build_surface(c.surface, base_dir()));
    }
    CHECK(names.size() >= 20);
}

TEST_CASE("Library: dropping every component into a real scene works, as the panel does it") {
    // The panel adds the material (when absent) and then the element. Do exactly that for every
    // entry in turn, into one scene, and require each to land.
    scrt::io::SceneDocument doc;
    doc.name = "library_drop_test";
    scrt::io::SunSourceDoc sun;
    sun.direction = scrt::math::vec3{0.0, 0.0, -1.0};
    doc.sources.push_back(sun);
    scrt::scene::SceneEditor ed(doc, base_dir());

    std::size_t dropped = 0;
    for (const auto& c : scrt::viz::builtin_components()) {
        CAPTURE(c.name);
        const auto& mats = scrt::viz::builtin_materials();
        const auto  it   = std::find_if(mats.begin(), mats.end(),
                                        [&](const scrt::viz::MaterialEntry& e) {
                                          return e.name == c.material;
                                      });
        REQUIRE(it != mats.end());
        const bool present = std::any_of(ed.doc().materials.begin(), ed.doc().materials.end(),
                                         [&](const scrt::io::MaterialDoc& m) {
                                             return m.id == c.material;
                                         });
        if (!present) CHECK(ed.add_material(it->doc));

        scrt::io::ElementDoc el;
        el.name        = c.name;
        el.material_id = c.material;
        el.surface     = c.surface;
        std::uint64_t id = 0;
        CHECK_NOTHROW(id = ed.add_element(el));
        CHECK(id != 0);
        CHECK(ed.scene().surface_by_id(id) != nullptr);
        ++dropped;
    }
    CHECK(dropped == scrt::viz::builtin_components().size());

    // And the whole thing still writes and re-reads strictly: a scene built entirely out of
    // the library must survive a save, which is the point of the library being document-backed.
    const auto w = scrt::io::write_document(ed.doc());
    CHECK_NOTHROW(scrt::io::parse_document(w, /*strict=*/true));
    CHECK(scrt::io::write_document(scrt::io::parse_document(w, true)) == w);
}

TEST_CASE("Library: every source entry builds and at least one is a sun") {
    scrt::scene::Scene scene;
    bool saw_sun = false;
    for (const auto& s : scrt::viz::builtin_sources()) {
        CAPTURE(s.name);
        CHECK_FALSE(s.name.empty());
        CHECK_FALSE(s.note.empty());
        std::unique_ptr<scrt::sources::LightSource> built;
        CHECK_NOTHROW(built = scrt::io::build_source(s.doc, scene));
        REQUIRE(built);
        CHECK(built->total_power_w() >= 0.0);
        if (std::holds_alternative<scrt::io::SunSourceDoc>(s.doc)) saw_sun = true;
        else CHECK(built->total_power_w() > 0.0);   // a laser preset that emits nothing is a bug
    }
    CHECK(saw_sun);
}

TEST_CASE("Library: user materials round-trip through the file they are kept in") {
    // The panel can write this file; if the write path were absent the read path would be dead
    // weight, so both are exercised here rather than only the one the app happens to call.
    const auto path = scrt::viz::user_materials_path();
    const bool had  = std::filesystem::exists(path);
    std::string backup;
    if (had) {
        std::ifstream in(path);
        backup.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    std::vector<scrt::viz::MaterialEntry> mine;
    scrt::viz::MaterialEntry e;
    e.name         = "My tuned foil";
    e.group        = "My materials";
    e.note         = "Your own entry, kept from a scene.";
    e.doc.id       = e.name;
    e.doc.type     = "real_mirror";
    e.doc.params   = json{{"reflectance", 0.8123}, {"slope_error_mrad", 5.5}};
    e.user_defined = true;
    mine.push_back(e);
    // A built-in must not be written out as if the user had authored it.
    scrt::viz::MaterialEntry builtin = scrt::viz::builtin_materials().front();
    mine.push_back(builtin);

    REQUIRE(scrt::viz::save_user_materials(mine));
    const auto back = scrt::viz::load_user_materials();
    REQUIRE(back.size() == 1);
    CHECK(back[0].name == "My tuned foil");
    CHECK(back[0].user_defined);
    CHECK(back[0].doc.type == "real_mirror");
    CHECK(back[0].doc.params["reflectance"] == 0.8123);
    CHECK_NOTHROW(scrt::io::build_material(back[0].doc));

    // Restore whatever the machine had before, so running the tests does not eat a real library.
    if (had) {
        std::ofstream out(path);
        out << backup;
    } else {
        std::filesystem::remove(path);
    }
}
