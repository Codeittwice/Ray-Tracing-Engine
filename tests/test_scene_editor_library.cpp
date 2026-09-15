#include <doctest/doctest.h>
#include "scrt/io/SceneDocument.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/io/SceneWriter.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/materials/ThinDielectricPane.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/scene/SceneEditor.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/sources/SunSource.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <variant>

// Wave 3 Stage 1: the editor API the material and component libraries hang off. Structural
// edits only - the document owns what exists, the live objects are derived from it through the
// same builders a file load uses.

#ifndef SCRT_SOURCE_DIR
#define SCRT_SOURCE_DIR "."
#endif

using scrt::io::ElementDoc;
using scrt::io::MaterialDoc;
using scrt::io::PlaneDoc;
using scrt::io::SceneDocument;
using scrt::scene::SceneEditor;

namespace {

SceneDocument empty_doc() {
    SceneDocument doc;
    doc.name = "library_fixture";
    MaterialDoc mat;
    mat.id     = "abs";
    mat.type   = "absorber";
    mat.params = nlohmann::json::object();
    doc.materials.push_back(mat);
    scrt::io::SunSourceDoc sun;
    sun.direction = scrt::math::vec3{0.0, 0.0, -1.0};
    doc.sources.push_back(sun);
    return doc;
}

MaterialDoc mirror_doc(const char* id) {
    MaterialDoc m;
    m.id     = id;
    m.type   = "real_mirror";
    m.params = nlohmann::json{{"reflectance", 0.9}, {"slope_error_mrad", 2.0}};
    return m;
}

ElementDoc plane_bound_to(const char* material_id) {
    ElementDoc el;
    el.name        = "plate";
    el.material_id = material_id;
    el.surface     = PlaneDoc{0.5, 0.5};
    el.transform.translation = {0.0, 0.0, 0.2};
    return el;
}

std::filesystem::path base_dir() { return std::filesystem::path(SCRT_SOURCE_DIR) / "examples"; }

} // namespace

TEST_CASE("build_material derives every material type the parser accepts") {
    auto make = [](const char* type, nlohmann::json params) {
        MaterialDoc m;
        m.id = "m"; m.type = type; m.params = std::move(params);
        return scrt::io::build_material(m);
    };
    CHECK(dynamic_cast<scrt::materials::PerfectMirror*>(make("perfect_mirror", {}).get()));
    CHECK(dynamic_cast<scrt::materials::RealMirror*>(make("real_mirror", {{"reflectance", 0.8}}).get()));
    CHECK(dynamic_cast<scrt::materials::Absorber*>(make("absorber", nullptr).get()));  // null params
    CHECK(dynamic_cast<scrt::materials::ThinDielectricPane*>(
        make("thin_dielectric_pane", {{"n", 1.5}, {"thickness_m", 0.003}}).get()));
    auto d = make("dielectric", {{"n", 1.5}, {"sellmeier", "bk7"}});
    auto* di = dynamic_cast<scrt::materials::Dielectric*>(d.get());
    REQUIRE(di);
    CHECK(di->has_sellmeier());
    CHECK(d->name() == "m");
    CHECK_THROWS_AS(make("unobtainium", {}), std::runtime_error);
    CHECK_THROWS_AS(make("dielectric", {{"sellmeier", "diamond"}}), std::runtime_error);
}

TEST_CASE("SceneEditor: add_material makes a material an element can then bind") {
    SceneEditor ed(empty_doc(), base_dir());
    // Before: an element bound to "foil" cannot be built.
    CHECK_THROWS_AS(ed.add_element(plane_bound_to("foil")), std::runtime_error);
    CHECK(ed.doc().elements.empty());

    // The live scene holds one material more than the document: build_scene injects an
    // anonymous absorber for the receiver that has no MaterialDoc. Count relative to that.
    const auto n0 = ed.scene().materials().size();
    REQUIRE(ed.add_material(mirror_doc("foil")));
    CHECK(ed.doc().materials.size() == 2);
    CHECK(ed.scene().materials().size() == n0 + 1);
    CHECK(ed.scene().materials().back()->name() == "foil");
    CHECK(ed.dirty());

    const auto id = ed.add_element(plane_bound_to("foil"));
    CHECK(id != 0);
    CHECK(ed.scene().surface_by_id(id)->material() == ed.scene().materials().back().get());
}

TEST_CASE("SceneEditor: add_material refuses a duplicate or empty id and an unbuildable type") {
    SceneEditor ed(empty_doc(), base_dir());
    const auto n0 = ed.scene().materials().size();
    CHECK_FALSE(ed.add_material(mirror_doc("abs")));   // already exists
    MaterialDoc blank = mirror_doc("");
    CHECK_FALSE(ed.add_material(blank));
    MaterialDoc bad = mirror_doc("x");
    bad.type = "unobtainium";
    CHECK_THROWS_AS(ed.add_material(bad), std::runtime_error);
    CHECK(ed.doc().materials.size() == 1);
    CHECK(ed.scene().materials().size() == n0);
    CHECK_FALSE(ed.dirty());
}

TEST_CASE("SceneEditor: remove_material refuses a bound material and removes an unbound one") {
    SceneEditor ed(empty_doc(), base_dir());
    const auto n0 = ed.scene().materials().size();
    REQUIRE(ed.add_material(mirror_doc("foil")));
    const auto id = ed.add_element(plane_bound_to("foil"));

    CHECK_FALSE(ed.remove_material("foil"));          // bound by "plate"
    CHECK_FALSE(ed.remove_material("no_such"));
    CHECK(ed.scene().materials().size() == n0 + 1);

    REQUIRE(ed.remove_element(id));
    CHECK(ed.remove_material("foil"));
    CHECK(ed.doc().materials.size() == 1);
    CHECK(ed.scene().materials().size() == n0);
    CHECK(ed.scene().materials().front()->name() == "abs");
}

TEST_CASE("SceneEditor: an added material survives save and reload") {
    SceneEditor ed(empty_doc(), base_dir());
    REQUIRE(ed.add_material(mirror_doc("foil")));
    const auto back = scrt::io::parse_document(scrt::io::write_document(ed.doc()), true);
    REQUIRE(back.materials.size() == 2);
    CHECK(back.materials[1].id == "foil");
    CHECK(back.materials[1].params["reflectance"] == 0.9);
    const auto ls = scrt::io::build_scene(back, base_dir());
    CHECK(ls.scene->materials().size() == ed.scene().materials().size());
}

TEST_CASE("SceneEditor: add_source appends a laser that traces, index-aligned with the document") {
    SceneEditor ed(empty_doc(), base_dir());
    REQUIRE(ed.scene().sources().size() == 1);

    scrt::io::LaserSourceDoc l;
    l.origin = {0.0, 0.0, 1.0};
    l.direction = {0.0, 0.0, -1.0};
    l.power_w = 2.0;
    l.beam_diameter_m = 0.0;
    const auto idx = ed.add_source(l);
    CHECK(idx == 1);
    CHECK(ed.doc().sources.size() == 2);
    CHECK(ed.scene().sources().size() == 2);
    CHECK(std::holds_alternative<scrt::io::LaserSourceDoc>(ed.doc().sources[1]));
    CHECK(std::string(ed.scene().sources()[1]->type_name()) == "laser");
    CHECK(ed.scene().sources()[1]->total_power_w() == 2.0);
    CHECK(ed.dirty());

    // The document is what a save writes, and it must build back to the same two sources.
    const auto back = scrt::io::parse_document(scrt::io::write_document(ed.doc()), true);
    REQUIRE(back.sources.size() == 2);
    const auto ls = scrt::io::build_scene(back, base_dir());
    CHECK(ls.scene->sources().size() == 2);
}

TEST_CASE("SceneEditor: a sun added with auto_fit gets an aperture fitted to the scene") {
    SceneDocument doc = empty_doc();
    doc.sources.clear();   // start with no source at all
    SceneEditor ed(doc, base_dir());
    CHECK(ed.scene().primary_sun() == nullptr);
    ed.add_element(plane_bound_to("abs"));

    scrt::io::SunSourceDoc sun;
    sun.direction = scrt::math::vec3{0.0, 0.0, -1.0};
    sun.aperture.mode = "auto_fit";
    const auto idx = ed.add_source(sun);
    CHECK(idx == 0);
    REQUIRE(ed.scene().primary_sun() != nullptr);
    // Fitted, not the struct default: the plate is 1 m wide, so the disk must cover it.
    CHECK(ed.scene().primary_sun()->aperture().radius > 0.5);
    CHECK(ed.scene().display_aperture() == &ed.scene().primary_sun()->aperture());
}

TEST_CASE("SceneEditor: remove_source drops document and live source together") {
    SceneEditor ed(empty_doc(), base_dir());
    scrt::io::LaserSourceDoc l;
    l.origin = {0.0, 0.0, 1.0};
    l.direction = {0.0, 0.0, -1.0};
    ed.add_source(l);
    REQUIRE(ed.scene().sources().size() == 2);

    CHECK_FALSE(ed.remove_source(5));
    CHECK(ed.remove_source(0));   // the sun
    CHECK(ed.doc().sources.size() == 1);
    CHECK(ed.scene().sources().size() == 1);
    CHECK(std::holds_alternative<scrt::io::LaserSourceDoc>(ed.doc().sources[0]));
    CHECK(ed.scene().primary_sun() == nullptr);
    CHECK(ed.scene().display_aperture() == nullptr);
}
