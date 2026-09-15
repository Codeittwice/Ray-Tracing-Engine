#include "scrt/io/SceneLoader.hpp"
#include "scrt/core/AABB.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/MeshImporter.hpp"
#include "scrt/io/SceneDocument.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/materials/ThinDielectricPane.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Buie.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/surfaces/CylindricalParaboloid.hpp"
#include "scrt/surfaces/FresnelZoneLens.hpp"
#include "scrt/surfaces/GeneralQuadric.hpp"
#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/surfaces/Sphere.hpp"
#include "scrt/surfaces/TriangleMesh.hpp"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

// NOTE ON SCOPE: this translation unit owns the *Scene-construction* half of scene loading -
// build_scene() plus the thin load_scene() file wrapper. The JSON-reading half lives in
// SceneDocument.cpp (parse_document / TransformDoc::to_transform) and serialization in
// SceneWriter.cpp (write_document).

namespace scrt::io {

using json = nlohmann::json;

namespace {

// ---- Shared helpers -----------------------------------------------------

/// Throws a "SceneLoader: "-prefixed runtime_error when cond is false.
void require(bool cond, const std::string& msg) {
    if (!cond)
        throw std::runtime_error("SceneLoader: " + msg);
}

/// Builds a world-from-local frame with the given origin and (renormalized) basis axes.
core::Transform frame_transform(math::vec3 origin, math::vec3 x_axis,
                                math::vec3 y_axis, math::vec3 z_axis) {
    math::mat4 m(1.0);
    m[0] = math::vec4(glm::normalize(x_axis), 0.0);
    m[1] = math::vec4(glm::normalize(y_axis), 0.0);
    m[2] = math::vec4(glm::normalize(z_axis), 0.0);
    m[3] = math::vec4(origin, 1.0);
    return core::Transform::from_matrix(m);
}

} // namespace

/// True when every field of t is still its struct default, i.e. the authored JSON had no
/// "transform" key at all (parse_document leaves an absent transform default-constructed).
///
/// Legacy SceneLoader.cpp only ever called parse_transform() when the "transform" key was
/// present (`if (el.contains("transform")) ...`), so an absent key never went through
/// core::Transform construction at all. TransformDoc::to_transform() is implemented by a
/// sibling agent concurrently with this file; rather than assume it reproduces a default
/// core::Transform bit-for-bit, this guard reproduces the legacy conditional exactly, so the
/// identity case never depends on that function.
bool is_default_transform(const TransformDoc& t) {
    return t.translation == math::vec3{0.0} &&
           t.rotation_euler_deg == math::vec3{0.0} &&
           t.scale == math::vec3{1.0} &&
           !t.matrix.has_value();
}

/// Constructs the surface geometry for one element/receiver-plane, dispatching on SurfaceDoc's
/// active alternative. Mirrors SceneLoader.cpp's former parse_surface() branch-for-branch.
std::unique_ptr<surfaces::Surface> build_surface(const SurfaceDoc& sd,
                                                  const std::filesystem::path& base_dir) {
    return std::visit(
        [&](const auto& s) -> std::unique_ptr<surfaces::Surface> {
            using T = std::decay_t<decltype(s)>;
            if constexpr (std::is_same_v<T, PlaneDoc>) {
                return std::make_unique<surfaces::Plane>(s.half_width, s.half_height);
            } else if constexpr (std::is_same_v<T, SphereDoc>) {
                return std::make_unique<surfaces::Sphere>(s.radius);
            } else if constexpr (std::is_same_v<T, ParaboloidDoc>) {
                return std::make_unique<surfaces::Paraboloid>(s.focal_length_m,
                                                               s.aperture_radius_m);
            } else if constexpr (std::is_same_v<T, CylParaboloidDoc>) {
                return std::make_unique<surfaces::CylindricalParaboloid>(
                    s.focal_length_m, s.aperture_half_width_m, s.aperture_half_length_m);
            } else if constexpr (std::is_same_v<T, QuadricDoc>) {
                surfaces::QuadricCoeffs c;
                c.A = s.A; c.B = s.B; c.C = s.C; c.D = s.D; c.E = s.E;
                c.F = s.F; c.G = s.G; c.H = s.H; c.I = s.I; c.J = s.J;
                return std::make_unique<surfaces::GeneralQuadric>(
                    c, core::AABB{s.box_min, s.box_max});
            } else if constexpr (std::is_same_v<T, FresnelZoneLensDoc>) {
                return std::make_unique<surfaces::FresnelZoneLens>(
                    s.focal_length_m, s.inner_radius_m, s.pitch_m, s.n_zones, s.n_lens);
            } else if constexpr (std::is_same_v<T, MeshDoc>) {
                auto imp = import_mesh(base_dir / s.path, s.scale_to_meters);
                return std::make_unique<surfaces::TriangleMesh>(std::move(imp.vertices),
                                                                  std::move(imp.indices));
            }
        },
        sd);
}


/// Builds a fully-wired LoadedScene from an already-parsed SceneDocument, keeping a copy of the
/// document in the result; see the Doxygen on its declaration in SceneDocument.hpp. Implemented
/// here (not in SceneDocument.cpp) by wave assignment: this is the Scene-construction half of the
/// former monolithic load_scene().
LoadedScene build_scene(const SceneDocument& doc, const std::filesystem::path& base_dir) {
    auto scene = std::make_unique<scene::Scene>();

    // ---- Materials ----------------------------------------------------
    std::unordered_map<std::string, const materials::Material*> mat_index;
    for (const auto& md : doc.materials) {
        const json& params = md.params;

        std::unique_ptr<materials::Material> mat;
        if (md.type == "perfect_mirror") {
            mat = std::make_unique<materials::PerfectMirror>();
        } else if (md.type == "real_mirror") {
            double rho = params.value("reflectance", 1.0);
            double se  = params.value("slope_error_mrad", 0.0);
            mat = std::make_unique<materials::RealMirror>(rho, se);
        } else if (md.type == "dielectric") {
            double n     = params.value("n", 1.5);
            double alpha = params.value("absorption_per_m", 0.0);
            auto di = std::make_unique<materials::Dielectric>(n, alpha);
            if (params.contains("sellmeier")) {
                std::string preset = params["sellmeier"].get<std::string>();
                if (preset == "bk7")
                    di->set_sellmeier(materials::SellmeierCoeffs::bk7());
                else if (preset == "fused_silica")
                    di->set_sellmeier(materials::SellmeierCoeffs::fused_silica());
                else
                    throw std::runtime_error(
                        "SceneLoader: unknown Sellmeier preset '" + preset + "'");
            }
            if (params.contains("alpha_spectrum")) {
                std::vector<std::pair<double, double>> spec;
                for (const auto& pt : params["alpha_spectrum"]) {
                    require(pt.is_array() && pt.size() == 2,
                            "alpha_spectrum entry must be [wavelength_nm, alpha_per_m]");
                    spec.push_back({pt[0].get<double>(), pt[1].get<double>()});
                }
                di->set_alpha_spectrum(std::move(spec));
            }
            mat = std::move(di);
        } else if (md.type == "thin_dielectric_pane") {
            const double n         = params.value("n", 1.49);
            const double thickness = params.value("thickness_m", 0.003);
            const double alpha     = params.value("absorption_per_m", 0.0);
            mat = std::make_unique<materials::ThinDielectricPane>(n, thickness, alpha);
        } else if (md.type == "absorber") {
            mat = std::make_unique<materials::Absorber>();
        } else {
            throw std::runtime_error("SceneLoader: unknown material type '" + md.type + "'");
        }
        mat->set_name(md.id);
        mat_index[md.id] = mat.get();
        scene->add_material(std::move(mat));
    }

    // ---- Elements / surfaces -------------------------------------------
    for (const auto& el : doc.elements) {
        auto surf = build_surface(el.surface, base_dir);

        if (!is_default_transform(el.transform))
            surf->set_transform(el.transform.to_transform());
        if (!el.name.empty())
            surf->set_name(el.name);

        require(mat_index.count(el.material_id) > 0,
                "element references unknown material id '" + el.material_id + "'");
        surf->set_material(mat_index.at(el.material_id));

        // TODO(Wave 3): ElementDoc::visible has no runtime counterpart on surfaces::Surface
        // yet. The surface is added regardless of visibility so the optics are unaffected by
        // an editor-only flag; wire real hide/show support in the Wave 3 editor/viewer.
        auto* raw = surf.get();
        // Scene::add_surface stamps its own sequential id; overwrite it with the document's so
        // outliner selection survives a save/load round trip even once ids develop gaps. Guarded
        // because id 0 is Surface's "not owned by a scene" sentinel: a hand-built SceneDocument
        // that never went through parse_document leaves it 0, and stamping that back would make
        // Scene::surface_by_id/index_of unable to find the surface.
        scene->add_surface(std::move(surf));
        if (el.id != 0)
            raw->set_id(el.id);
    }

    // ---- Receiver -------------------------------------------------------
    if (std::holds_alternative<BoxReceiverDoc>(doc.receiver.kind)) {
        const auto& bd = std::get<BoxReceiverDoc>(doc.receiver.kind);
        const double half_width  = bd.half_width;
        const double half_height = bd.half_height;
        const double depth       = bd.depth;
        const int    nx = bd.nx, ny = bd.ny;

        const double bin_x = (2.0 * half_width) / static_cast<double>(nx);
        const double bin_y = (2.0 * half_height) / static_cast<double>(ny);
        const double bin   = 0.5 * (bin_x + bin_y);
        const int depth_bins = std::max(1, static_cast<int>(std::lround(depth / bin)));

        auto recv = std::make_unique<scene::Receiver>(half_width, half_height, nx, ny);
        recv->mutable_faces().front()->set_name("glass_top");
        recv->mutable_faces().front()->set_mode(scene::ReceiverFaceMode::RecordPass);

        auto absorber = std::make_unique<materials::Absorber>();
        const auto* absorber_ptr = absorber.get();
        scene->add_material(std::move(absorber));

        recv->mutable_faces().front()->surface()->set_material(absorber_ptr);
        recv->mutable_faces().front()->set_transform(
            frame_transform({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}));

        auto& bottom = recv->add_face("bottom", half_width, half_height, nx, ny,
                                      scene::ReceiverFaceMode::RecordAbsorb);
        bottom.surface()->set_material(absorber_ptr);
        bottom.set_transform(
            frame_transform({0.0, 0.0, -depth}, {1.0, 0.0, 0.0}, {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}));

        auto& north = recv->add_face("north_wall", half_width, depth * 0.5, nx, depth_bins,
                                     scene::ReceiverFaceMode::RecordAbsorb);
        north.surface()->set_material(absorber_ptr);
        north.set_transform(
            frame_transform({0.0, half_height, -depth * 0.5}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}));

        auto& south = recv->add_face("south_wall", half_width, depth * 0.5, nx, depth_bins,
                                     scene::ReceiverFaceMode::RecordAbsorb);
        south.surface()->set_material(absorber_ptr);
        south.set_transform(
            frame_transform({0.0, -half_height, -depth * 0.5}, {1.0, 0.0, 0.0}, {0.0, 0.0, -1.0}, {0.0, -1.0, 0.0}));

        auto& east = recv->add_face("east_wall", depth * 0.5, half_height, depth_bins, ny,
                                    scene::ReceiverFaceMode::RecordAbsorb);
        east.surface()->set_material(absorber_ptr);
        east.set_transform(
            frame_transform({half_width, 0.0, -depth * 0.5}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}));

        auto& west = recv->add_face("west_wall", depth * 0.5, half_height, depth_bins, ny,
                                    scene::ReceiverFaceMode::RecordAbsorb);
        west.surface()->set_material(absorber_ptr);
        west.set_transform(
            frame_transform({-half_width, 0.0, -depth * 0.5}, {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}));

        if (bd.battery.has_value() && bd.battery->enabled) {
            const auto& bat = *bd.battery;
            const double battery_hw     = bat.half_width.value_or(half_width * 0.5);
            const double battery_hh     = bat.half_height.value_or(half_height * 0.5);
            const double top_depth      = bat.top_depth_m.value_or(depth);
            const double battery_height = bat.height_m.value_or(depth - top_depth);
            require(top_depth > 0.0 && top_depth <= depth,
                    "box battery top_depth_m must be in (0, depth]");
            require(battery_height > 0.0 && top_depth + battery_height <= depth + 1.0e-12,
                    "box battery height_m must keep the battery inside the box depth");
            const int battery_nx = bat.nx.value_or(nx);
            const int battery_ny = bat.ny.value_or(ny);
            const double battery_bin_x = (2.0 * battery_hw) / static_cast<double>(battery_nx);
            const double battery_bin_y = (2.0 * battery_hh) / static_cast<double>(battery_ny);
            const double battery_bin   = 0.5 * (battery_bin_x + battery_bin_y);
            const int battery_height_bins =
                std::max(1, static_cast<int>(std::lround(battery_height / battery_bin)));

            auto& battery = recv->add_face("battery_top", battery_hw, battery_hh,
                                           battery_nx, battery_ny,
                                           scene::ReceiverFaceMode::RecordAbsorb);
            battery.surface()->set_material(absorber_ptr);
            battery.set_transform(
                frame_transform({0.0, 0.0, -top_depth}, {1.0, 0.0, 0.0},
                                {0.0, 1.0, 0.0}, {0.0, 0.0, 1.0}));

            const double side_z = -(top_depth + battery_height * 0.5);
            auto& battery_north =
                recv->add_face("battery_north_wall", battery_hw, battery_height * 0.5,
                               battery_nx, battery_height_bins,
                               scene::ReceiverFaceMode::RecordAbsorb);
            battery_north.surface()->set_material(absorber_ptr);
            battery_north.set_transform(
                frame_transform({0.0, battery_hh, side_z}, {1.0, 0.0, 0.0},
                                {0.0, 0.0, -1.0}, {0.0, 1.0, 0.0}));

            auto& battery_south =
                recv->add_face("battery_south_wall", battery_hw, battery_height * 0.5,
                               battery_nx, battery_height_bins,
                               scene::ReceiverFaceMode::RecordAbsorb);
            battery_south.surface()->set_material(absorber_ptr);
            battery_south.set_transform(
                frame_transform({0.0, -battery_hh, side_z}, {1.0, 0.0, 0.0},
                                {0.0, 0.0, -1.0}, {0.0, -1.0, 0.0}));

            auto& battery_east =
                recv->add_face("battery_east_wall", battery_height * 0.5, battery_hh,
                               battery_height_bins, battery_ny,
                               scene::ReceiverFaceMode::RecordAbsorb);
            battery_east.surface()->set_material(absorber_ptr);
            battery_east.set_transform(
                frame_transform({battery_hw, 0.0, side_z}, {0.0, 0.0, -1.0},
                                {0.0, 1.0, 0.0}, {1.0, 0.0, 0.0}));

            auto& battery_west =
                recv->add_face("battery_west_wall", battery_height * 0.5, battery_hh,
                               battery_height_bins, battery_ny,
                               scene::ReceiverFaceMode::RecordAbsorb);
            battery_west.surface()->set_material(absorber_ptr);
            battery_west.set_transform(
                frame_transform({-battery_hw, 0.0, side_z}, {0.0, 0.0, -1.0},
                                {0.0, 1.0, 0.0}, {-1.0, 0.0, 0.0}));
        }

        // NOTE(behaviour change, flagged for lead ruling): legacy SceneLoader.cpp threw
        // std::runtime_error when receiver.top_mode != "record_pass" (SceneLoader.cpp:409-411).
        // ReceiverDoc deliberately does not store top_mode (parse_document only ever accepted
        // the literal "record_pass", so the key carries no round-trippable information), so
        // that validation cannot be reproduced here without inventing a field the frozen header
        // does not declare. The check is dropped rather than invented.

        const core::Transform base = is_default_transform(doc.receiver.transform)
                                          ? core::Transform{}
                                          : doc.receiver.transform.to_transform();
        for (auto& face : recv->mutable_faces()) {
            const auto local = face->surface()->transform();
            face->set_transform(base.compose(local));
        }
        scene->set_receiver(std::move(recv));
    } else {
        const auto& pd = std::get<PlaneReceiverDoc>(doc.receiver.kind);
        auto recv = std::make_unique<scene::Receiver>(pd.half_width, pd.half_height, pd.nx, pd.ny);

        // Dedicated absorber for the receiver plane.
        auto absorber = std::make_unique<materials::Absorber>();
        recv->surface()->set_material(absorber.get());
        scene->add_material(std::move(absorber));

        if (!is_default_transform(doc.receiver.transform))
            recv->set_transform(doc.receiver.transform.to_transform());
        scene->set_receiver(std::move(recv));
    }

    // ---- Sun --------------------------------------------------------------
    {
        std::unique_ptr<sources::SunSource> sun;
        if (doc.sun.sunshape_type == "pillbox") {
            sun = std::make_unique<sources::Pillbox>(doc.sun.half_angle_mrad * 1e-3);
        } else if (doc.sun.sunshape_type == "buie") {
            sun = std::make_unique<sources::Buie>(doc.sun.chi);
        } else {
            throw std::runtime_error(
                "SceneLoader: unknown sunshape type '" + doc.sun.sunshape_type + "'");
        }

        if (doc.sun.direction.has_value())
            sun->set_sun_direction(math::safe_normalize(*doc.sun.direction));
        else
            sun->set_sun_angles(sources::SunAngles{doc.sun.azimuth_deg, doc.sun.elevation_deg});
        sun->set_dni(doc.sun.dni_wm2);

        // ---- Aperture: the sun's own, resolved here because auto_fit unions the world
        // bounds, so it must come after every surface and the receiver are in the scene.
        // world_bounds() does not include apertures, so there is no circularity.
        if (doc.aperture.mode == "auto" || doc.aperture.mode == "auto_fit") {
            scene::Aperture ap = scene::Aperture::auto_fit(
                scene->world_bounds(), sun->to_sun(), doc.aperture.margin);
            ap.mode = scene::ApertureMode::AutoFitToSun;
            sun->set_aperture(ap);
        } else {
            // "fixed" and any unrecognized value: forward-compatible passthrough, build a
            // fixed disk exactly as the legacy loader always did.
            scene::Aperture ap;
            ap.center = doc.aperture.center;
            ap.normal = math::safe_normalize(doc.aperture.normal);
            ap.radius = doc.aperture.radius;
            ap.mode   = scene::ApertureMode::Fixed;
            ap.margin = doc.aperture.margin;
            sun->set_aperture(ap);
        }
        scene->add_source(std::move(sun));
    }

    // ---- Trace config and finish -------------------------------------------
    tracer::TraceConfig cfg = doc.trace;
    scene->build_acceleration_structure();

    // The document is copied into the result rather than discarded: it is the only structural
    // description of the scene, and without it a caller that loaded a file could never save it
    // back. The copy is pure metadata (names, shape parameters, mesh *paths*) - imported mesh
    // geometry lives in the surfaces, not here - so it costs kilobytes, not megabytes.
    return {std::move(scene), cfg, doc};
}

/// Parse a JSON scene file and return a fully wired Scene; see Doxygen in SceneLoader.hpp.
LoadedScene load_scene(const std::filesystem::path& path) {
    std::ifstream file(path);
    require(file.is_open(), "cannot open '" + path.string() + "'");

    json root;
    try {
        root = json::parse(file, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("SceneLoader JSON parse error: ") + e.what());
    }

    return build_scene(parse_document(root), path.parent_path());
}

} // namespace scrt::io
