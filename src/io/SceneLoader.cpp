#include "scrt/io/SceneLoader.hpp"
#include "scrt/core/AABB.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/MeshImporter.hpp"
#include "scrt/materials/Absorber.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/PerfectMirror.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/Pillbox.hpp"
#include "scrt/surfaces/GeneralQuadric.hpp"
#include "scrt/surfaces/Paraboloid.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/surfaces/Sphere.hpp"
#include "scrt/surfaces/TriangleMesh.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace scrt::io {

using json = nlohmann::json;

namespace {

void require(bool cond, const std::string& msg) {
    if (!cond)
        throw std::runtime_error("SceneLoader: " + msg);
}

math::vec3 read_vec3(const json& j, const std::string& key) {
    require(j.contains(key), "missing '" + key + "'");
    require(j[key].is_array() && j[key].size() == 3,
            "'" + key + "' must be a 3-element array");
    return {j[key][0].get<double>(), j[key][1].get<double>(), j[key][2].get<double>()};
}

core::Transform parse_transform(const json& j) {
    core::Transform rot;
    if (j.contains("rotation_euler_deg")) {
        math::vec3 deg = read_vec3(j, "rotation_euler_deg");
        rot = core::Transform::from_euler_xyz(deg * math::DEG2RAD);
    }
    if (j.contains("translation")) {
        math::vec3 t = read_vec3(j, "translation");
        // T * R: rotate first, then translate (standard object placement).
        return core::Transform::from_translation(t).compose(rot);
    }
    return rot;
}

std::unique_ptr<surfaces::Surface> parse_surface(const json& sj,
                                                   const std::filesystem::path& base_dir) {
    require(sj.contains("type"), "surface missing 'type'");
    std::string type = sj["type"];

    if (type == "plane") {
        double hw = sj.value("half_width",  0.5);
        double hh = sj.value("half_height", 0.5);
        return std::make_unique<surfaces::Plane>(hw, hh);
    }
    if (type == "sphere") {
        require(sj.contains("radius"), "sphere missing 'radius'");
        return std::make_unique<surfaces::Sphere>(sj["radius"].get<double>());
    }
    if (type == "paraboloid") {
        require(sj.contains("focal_length_m"),    "paraboloid missing 'focal_length_m'");
        require(sj.contains("aperture_radius_m"), "paraboloid missing 'aperture_radius_m'");
        return std::make_unique<surfaces::Paraboloid>(
            sj["focal_length_m"].get<double>(),
            sj["aperture_radius_m"].get<double>());
    }
    if (type == "quadric") {
        require(sj.contains("coeffs"),       "quadric missing 'coeffs'");
        require(sj.contains("aperture_box"), "quadric missing 'aperture_box'");
        const json& cj = sj["coeffs"];
        surfaces::QuadricCoeffs c;
        c.A = cj.value("A", 0.0); c.B = cj.value("B", 0.0); c.C = cj.value("C", 0.0);
        c.D = cj.value("D", 0.0); c.E = cj.value("E", 0.0); c.F = cj.value("F", 0.0);
        c.G = cj.value("G", 0.0); c.H = cj.value("H", 0.0); c.I = cj.value("I", 0.0);
        c.J = cj.value("J", 0.0);
        const json& bj = sj["aperture_box"];
        require(bj.contains("min") && bj.contains("max"),
                "quadric aperture_box missing 'min'/'max'");
        math::vec3 bmin{bj["min"][0].get<double>(),
                        bj["min"][1].get<double>(),
                        bj["min"][2].get<double>()};
        math::vec3 bmax{bj["max"][0].get<double>(),
                        bj["max"][1].get<double>(),
                        bj["max"][2].get<double>()};
        return std::make_unique<surfaces::GeneralQuadric>(c, core::AABB{bmin, bmax});
    }
    if (type == "mesh") {
        require(sj.contains("path"), "mesh surface missing 'path'");
        double scale = sj.value("scale_to_meters", 1.0);
        auto imp = import_mesh(base_dir / sj["path"].get<std::string>(), scale);
        return std::make_unique<surfaces::TriangleMesh>(
            std::move(imp.vertices), std::move(imp.indices));
    }
    throw std::runtime_error("SceneLoader: unknown surface type '" + type + "'");
}

} // namespace

LoadedScene load_scene(const std::filesystem::path& path) {
    std::filesystem::path base_dir = path.parent_path();
    std::ifstream file(path);
    require(file.is_open(), "cannot open '" + path.string() + "'");

    json root;
    try {
        root = json::parse(file, nullptr, /*exceptions=*/true, /*ignore_comments=*/true);
    } catch (const json::parse_error& e) {
        throw std::runtime_error(std::string("SceneLoader JSON parse error: ") + e.what());
    }

    require(root.contains("scene"), "missing top-level 'scene' key");
    const json& s = root["scene"];

    auto scene = std::make_unique<scene::Scene>();

    // ---- Materials --------------------------------------------------------
    std::unordered_map<std::string, const materials::Material*> mat_index;
    if (s.contains("materials")) {
        for (const auto& mj : s["materials"]) {
            require(mj.contains("id"),   "material entry missing 'id'");
            require(mj.contains("type"), "material entry missing 'type'");
            std::string id   = mj["id"];
            std::string type = mj["type"];

            std::unique_ptr<materials::Material> mat;
            if (type == "perfect_mirror") {
                mat = std::make_unique<materials::PerfectMirror>();
            } else if (type == "real_mirror") {
                double rho = mj.value("reflectance",     1.0);
                double se  = mj.value("slope_error_mrad", 0.0);
                mat = std::make_unique<materials::RealMirror>(rho, se);
            } else if (type == "dielectric") {
                require(mj.contains("n"), "dielectric material missing 'n'");
                double n     = mj["n"].get<double>();
                double alpha = mj.value("absorption_per_m", 0.0);
                mat = std::make_unique<materials::Dielectric>(n, alpha);
            } else if (type == "absorber") {
                mat = std::make_unique<materials::Absorber>();
            } else {
                throw std::runtime_error("SceneLoader: unknown material type '" + type + "'");
            }
            mat->set_name(id);
            mat_index[id] = mat.get();
            scene->add_material(std::move(mat));
        }
    }

    // ---- Sun --------------------------------------------------------------
    require(s.contains("sun"), "missing 'sun'");
    {
        const json& sj = s["sun"];
        require(sj.contains("sunshape"), "sun missing 'sunshape'");
        std::string shape_type = sj["sunshape"].value("type", "pillbox");
        require(shape_type == "pillbox",
                "only 'pillbox' sunshape is supported in Phase 4; got '" + shape_type + "'");
        double ha_mrad = sj["sunshape"].value("half_angle_mrad", 4.65);
        auto sun = std::make_unique<sources::Pillbox>(ha_mrad * 1e-3);
        sun->set_sun_direction(math::safe_normalize(read_vec3(sj, "direction")));
        sun->set_dni(sj.value("dni_wm2", 1000.0));
        scene->set_sun(std::move(sun));
    }

    // ---- Aperture ---------------------------------------------------------
    require(s.contains("aperture"), "missing 'aperture'");
    {
        const json& aj = s["aperture"];
        scene::Aperture ap;
        ap.center = read_vec3(aj, "center");
        ap.normal = math::safe_normalize(read_vec3(aj, "normal"));
        ap.radius = aj.value("radius", 1.0);
        scene->set_aperture(ap);
    }

    // ---- Elements ---------------------------------------------------------
    if (s.contains("elements")) {
        for (const auto& el : s["elements"]) {
            require(el.contains("surface"),  "element missing 'surface'");
            require(el.contains("material"), "element missing 'material'");

            auto surf = parse_surface(el["surface"], base_dir);
            if (el.contains("transform"))
                surf->set_transform(parse_transform(el["transform"]));
            if (el.contains("name"))
                surf->set_name(el["name"].get<std::string>());

            std::string mat_id = el["material"];
            require(mat_index.count(mat_id) > 0,
                    "element references unknown material id '" + mat_id + "'");
            surf->set_material(mat_index.at(mat_id));

            scene->add_surface(std::move(surf));
        }
    }

    // ---- Receiver ---------------------------------------------------------
    require(s.contains("receiver"), "missing 'receiver'");
    {
        const json& rj = s["receiver"];
        int nx = 64, ny = 64;
        if (rj.contains("grid")) {
            nx = rj["grid"].value("nx", nx);
            ny = rj["grid"].value("ny", ny);
        }
        double hw = 0.05, hh = 0.05;
        if (rj.contains("surface")) {
            hw = rj["surface"].value("half_width",  hw);
            hh = rj["surface"].value("half_height", hh);
        }
        auto recv = std::make_unique<scene::Receiver>(hw, hh, nx, ny);

        // Dedicated absorber for the receiver plane.
        auto absorber = std::make_unique<materials::Absorber>();
        recv->surface()->set_material(absorber.get());
        scene->add_material(std::move(absorber));

        if (rj.contains("transform"))
            recv->set_transform(parse_transform(rj["transform"]));
        scene->set_receiver(std::move(recv));
    }

    // ---- Trace config -----------------------------------------------------
    tracer::TraceConfig cfg;
    if (root.contains("trace")) {
        const json& tj = root["trace"];
        if (tj.contains("n_primary_rays"))
            cfg.n_primary_rays = tj["n_primary_rays"].get<std::size_t>();
        if (tj.contains("max_bounces"))
            cfg.max_bounces = tj["max_bounces"].get<int>();
        if (tj.contains("power_cutoff_w"))
            cfg.power_cutoff_w = tj["power_cutoff_w"].get<double>();
        if (tj.contains("rng_seed"))
            cfg.rng_seed = tj["rng_seed"].get<std::uint64_t>();
        if (tj.contains("record_paths"))
            cfg.record_paths = tj["record_paths"].get<bool>();
        if (tj.contains("max_paths_to_record"))
            cfg.max_paths_to_record = tj["max_paths_to_record"].get<std::size_t>();
    }

    scene->build_acceleration_structure();

    return {std::move(scene), cfg};
}

} // namespace scrt::io
