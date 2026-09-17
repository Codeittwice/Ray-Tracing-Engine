#include "scrt/io/SceneDocument.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/sources/SunSource.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// NOTE ON SCOPE: this translation unit implements only TransformDoc::to_transform() and
// parse_document(). write_document()/save_scene() are implemented by agent "Writer" in
// SceneWriter.cpp; build_scene() is implemented by agent "SceneBuilder" in SceneLoader.cpp.
// The frozen header's Doxygen comments claim all four live here — that comment is stale.

namespace scrt::io {

using json = nlohmann::json;

// ---- core::Transform construction --------------------------------------

/// Builds the equivalent core::Transform: an explicit matrix wins over the TRS fields;
/// otherwise composes as T * R(euler XYZ) * S, mirroring core::Transform::from_trs.
core::Transform TransformDoc::to_transform() const {
    if (matrix)
        return core::Transform::from_matrix(*matrix);
    return core::Transform::from_trs(translation, rotation_euler_deg * math::DEG2RAD, scale);
}

namespace {

/// Throws a "SceneLoader: "-prefixed runtime_error when cond is false.
void require(bool cond, const std::string& msg) {
    if (!cond)
        throw std::runtime_error("SceneLoader: " + msg);
}

/// Reads a required 3-element JSON array field as a math::vec3; throws when absent/malformed.
math::vec3 read_vec3(const json& j, const std::string& key) {
    require(j.contains(key), "missing '" + key + "'");
    require(j[key].is_array() && j[key].size() == 3,
            "'" + key + "' must be a 3-element array");
    return {j[key][0].get<double>(), j[key][1].get<double>(), j[key][2].get<double>()};
}

/// Strict-mode guard: throws when obj (a JSON object) contains a key outside `allowed`.
void reject_unknown_keys(const json& obj, std::initializer_list<const char*> allowed,
                          const char* context) {
    if (!obj.is_object())
        return;
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        bool ok = false;
        for (const char* key : allowed) {
            if (it.key() == key) {
                ok = true;
                break;
            }
        }
        if (!ok)
            throw std::runtime_error("SceneLoader: unrecognized key '" + it.key() +
                                      "' in " + context);
    }
}

/// Strict-mode-only: validates a material's type string and its (id/type-erased) param keys
/// against the fixed per-type allow-list, and rejects an unknown Sellmeier preset.
void validate_material_strict(const std::string& type, const json& params) {
    static const std::unordered_map<std::string, std::vector<std::string>> allowed = {
        {"perfect_mirror", {}},
        {"real_mirror", {"reflectance", "slope_error_mrad"}},
        {"dielectric", {"n", "absorption_per_m", "sellmeier", "alpha_spectrum"}},
        {"thin_dielectric_pane", {"n", "thickness_m", "absorption_per_m"}},
        {"absorber", {}},
        {"beam_splitter", {"reflectance", "absorptance"}},
        {"diffuser", {"albedo"}},
        {"polariser", {"transmission_axis_deg", "extinction_ratio", "transmission"}},
        {"waveplate", {"retardance_waves", "fast_axis_deg", "transmission"}},
        {"polarising_beam_splitter", {"extinction_ratio"}},
    };
    auto it = allowed.find(type);
    require(it != allowed.end(), "unknown material type '" + type + "'");
    for (auto p = params.begin(); p != params.end(); ++p) {
        if (std::find(it->second.begin(), it->second.end(), p.key()) == it->second.end())
            throw std::runtime_error("SceneLoader: unrecognized key '" + p.key() +
                                      "' in material of type '" + type + "'");
    }
    if (type == "dielectric" && params.contains("sellmeier")) {
        std::string preset = params["sellmeier"].get<std::string>();
        require(materials::SellmeierCoeffs::by_name(preset).has_value(),
                "unknown Sellmeier preset '" + preset + "'");
    }
}

/// Parses a `transform` JSON object into a TransformDoc; absent keys keep struct defaults
/// (identity translation/rotation, unit scale, no explicit matrix).
TransformDoc parse_transform_doc(const json& j, bool strict) {
    if (strict)
        reject_unknown_keys(j, {"translation", "rotation_euler_deg", "scale", "matrix"},
                             "transform");
    TransformDoc t;
    if (j.contains("rotation_euler_deg"))
        t.rotation_euler_deg = read_vec3(j, "rotation_euler_deg");
    if (j.contains("translation"))
        t.translation = read_vec3(j, "translation");
    if (j.contains("scale"))
        t.scale = read_vec3(j, "scale");
    if (j.contains("matrix")) {
        require(j["matrix"].is_array() && j["matrix"].size() == 16,
                "'matrix' must be a 16-element array");
        math::mat4 m;
        for (int i = 0; i < 16; ++i)
            m[i / 4][i % 4] = j["matrix"][i].get<double>();
        t.matrix = m;
    }
    return t;
}

/// Parses an `element.surface` JSON object into the matching SurfaceDoc alternative; does no
/// filesystem work — a "mesh" surface stores its path string verbatim, unresolved.
SurfaceDoc parse_surface_doc(const json& sj, bool strict) {
    require(sj.contains("type"), "surface missing 'type'");
    std::string type = sj["type"];

    if (type == "plane") {
        if (strict)
            reject_unknown_keys(sj, {"type", "half_width", "half_height"}, "surface.plane");
        PlaneDoc d;
        d.half_width = sj.value("half_width", 0.5);
        d.half_height = sj.value("half_height", 0.5);
        return d;
    }
    if (type == "thick_lens") {
        if (strict)
            reject_unknown_keys(sj, {"type", "radius1", "radius2", "center_thickness_m",
                                     "diameter_m"}, "surface.thick_lens");
        require(sj.contains("radius1"), "thick_lens missing 'radius1'");
        require(sj.contains("center_thickness_m"), "thick_lens missing 'center_thickness_m'");
        require(sj.contains("diameter_m"), "thick_lens missing 'diameter_m'");
        ThickLensDoc d;
        d.radius1 = sj["radius1"].get<double>();
        d.radius2 = sj.value("radius2", 0.0);
        d.center_thickness_m = sj["center_thickness_m"].get<double>();
        d.diameter_m = sj["diameter_m"].get<double>();
        require(d.center_thickness_m > 0.0 && d.diameter_m > 0.0,
                "thick_lens thickness and diameter must be > 0");
        for (double r : {d.radius1, d.radius2})
            require(r == 0.0 || std::abs(r) >= 0.5 * d.diameter_m,
                    "thick_lens: a nonzero radius must be at least the half-diameter");
        return d;
    }
    if (type == "disk") {
        if (strict)
            reject_unknown_keys(sj, {"type", "radius", "hole_radius"}, "surface.disk");
        require(sj.contains("radius"), "disk missing 'radius'");
        DiskDoc d;
        d.radius = sj["radius"].get<double>();
        d.hole_radius = sj.value("hole_radius", 0.0);
        require(std::isfinite(d.radius) && d.radius > 0.0, "disk 'radius' must be > 0");
        require(std::isfinite(d.hole_radius) && d.hole_radius >= 0.0 && d.hole_radius < d.radius,
                "disk 'hole_radius' must satisfy 0 <= hole_radius < radius");
        return d;
    }
    if (type == "slit_plate") {
        if (strict)
            reject_unknown_keys(sj, {"type", "half_width", "half_height", "slit_width",
                                     "slit_count", "slit_pitch"}, "surface.slit_plate");
        require(sj.contains("slit_width"), "slit_plate missing 'slit_width'");
        SlitPlateDoc d;
        d.half_width  = sj.value("half_width", 0.02);
        d.half_height = sj.value("half_height", 0.02);
        d.slit_width  = sj["slit_width"].get<double>();
        d.slit_count  = sj.value("slit_count", 1);
        d.slit_pitch  = sj.value("slit_pitch", 0.0);
        require(d.slit_count >= 1, "slit_plate 'slit_count' must be >= 1");
        require(d.slit_width > 0.0, "slit_plate 'slit_width' must be > 0");
        if (d.slit_count > 1) {
            require(sj.contains("slit_pitch"), "slit_plate with several slots needs 'slit_pitch'");
            require(d.slit_pitch > d.slit_width,
                    "slit_plate 'slit_pitch' must exceed 'slit_width' or the slots merge");
        }
        return d;
    }
    if (type == "sphere") {
        if (strict)
            reject_unknown_keys(sj, {"type", "radius"}, "surface.sphere");
        require(sj.contains("radius"), "sphere missing 'radius'");
        SphereDoc d;
        d.radius = sj["radius"].get<double>();
        return d;
    }
    if (type == "paraboloid") {
        if (strict)
            reject_unknown_keys(sj, {"type", "focal_length_m", "aperture_radius_m"},
                                 "surface.paraboloid");
        require(sj.contains("focal_length_m"), "paraboloid missing 'focal_length_m'");
        require(sj.contains("aperture_radius_m"), "paraboloid missing 'aperture_radius_m'");
        ParaboloidDoc d;
        d.focal_length_m = sj["focal_length_m"].get<double>();
        d.aperture_radius_m = sj["aperture_radius_m"].get<double>();
        return d;
    }
    if (type == "quadric") {
        if (strict)
            reject_unknown_keys(sj, {"type", "coeffs", "aperture_box"}, "surface.quadric");
        require(sj.contains("coeffs"), "quadric missing 'coeffs'");
        require(sj.contains("aperture_box"), "quadric missing 'aperture_box'");
        const json& cj = sj["coeffs"];
        if (strict)
            reject_unknown_keys(cj, {"A", "B", "C", "D", "E", "F", "G", "H", "I", "J"},
                                 "surface.quadric.coeffs");
        QuadricDoc d;
        d.A = cj.value("A", 0.0); d.B = cj.value("B", 0.0); d.C = cj.value("C", 0.0);
        d.D = cj.value("D", 0.0); d.E = cj.value("E", 0.0); d.F = cj.value("F", 0.0);
        d.G = cj.value("G", 0.0); d.H = cj.value("H", 0.0); d.I = cj.value("I", 0.0);
        d.J = cj.value("J", 0.0);
        const json& bj = sj["aperture_box"];
        require(bj.contains("min") && bj.contains("max"),
                "quadric aperture_box missing 'min'/'max'");
        if (strict)
            reject_unknown_keys(bj, {"min", "max"}, "surface.quadric.aperture_box");
        // Mirrors SceneLoader.cpp: raw [i].get<double>() indexing, not read_vec3 — malformed
        // arrays behave identically (bitwise) to the loader, including on out-of-range access.
        d.box_min = {bj["min"][0].get<double>(), bj["min"][1].get<double>(),
                     bj["min"][2].get<double>()};
        d.box_max = {bj["max"][0].get<double>(), bj["max"][1].get<double>(),
                     bj["max"][2].get<double>()};
        return d;
    }
    if (type == "fresnel_zone_lens") {
        if (strict)
            reject_unknown_keys(sj,
                                 {"type", "focal_length_m", "inner_radius_m", "pitch_m",
                                  "n_zones", "n_lens"},
                                 "surface.fresnel_zone_lens");
        require(sj.contains("focal_length_m"), "fresnel_zone_lens missing 'focal_length_m'");
        require(sj.contains("inner_radius_m"), "fresnel_zone_lens missing 'inner_radius_m'");
        require(sj.contains("pitch_m"), "fresnel_zone_lens missing 'pitch_m'");
        require(sj.contains("n_zones"), "fresnel_zone_lens missing 'n_zones'");
        require(sj.contains("n_lens"), "fresnel_zone_lens missing 'n_lens'");
        FresnelZoneLensDoc d;
        d.focal_length_m = sj["focal_length_m"].get<double>();
        d.inner_radius_m = sj["inner_radius_m"].get<double>();
        d.pitch_m = sj["pitch_m"].get<double>();
        d.n_zones = sj["n_zones"].get<int>();
        d.n_lens = sj["n_lens"].get<double>();
        return d;
    }
    if (type == "cylindrical_paraboloid") {
        if (strict)
            reject_unknown_keys(sj,
                                 {"type", "focal_length_m", "aperture_half_width_m",
                                  "aperture_half_length_m"},
                                 "surface.cylindrical_paraboloid");
        require(sj.contains("focal_length_m"),
                "cylindrical_paraboloid missing 'focal_length_m'");
        require(sj.contains("aperture_half_width_m"),
                "cylindrical_paraboloid missing 'aperture_half_width_m'");
        require(sj.contains("aperture_half_length_m"),
                "cylindrical_paraboloid missing 'aperture_half_length_m'");
        CylParaboloidDoc d;
        d.focal_length_m = sj["focal_length_m"].get<double>();
        d.aperture_half_width_m = sj["aperture_half_width_m"].get<double>();
        d.aperture_half_length_m = sj["aperture_half_length_m"].get<double>();
        return d;
    }
    if (type == "mesh") {
        if (strict)
            reject_unknown_keys(sj, {"type", "path", "scale_to_meters"}, "surface.mesh");
        require(sj.contains("path"), "mesh surface missing 'path'");
        MeshDoc d;
        d.path = sj["path"].get<std::string>();
        d.scale_to_meters = sj.value("scale_to_meters", 1.0);
        return d;
    }
    throw std::runtime_error("SceneLoader: unknown surface type '" + type + "'");
}

} // namespace

/// Parses an aperture block: the legacy `scene.aperture`, or a sun entry's own `aperture`.
ApertureDoc parse_aperture(const json& aj, bool strict) {
    if (strict)
        reject_unknown_keys(aj, {"type", "center", "normal", "radius", "mode", "margin"},
                            "aperture");
    ApertureDoc ap;
    ap.center = read_vec3(aj, "center");
    ap.normal = read_vec3(aj, "normal");
    ap.radius = aj.value("radius", 1.0);
    ap.mode = aj.value("mode", std::string("fixed"));
    ap.margin = aj.value("margin", 0.05);
    if (strict)
        require(ap.mode == "fixed" || ap.mode == "auto" || ap.mode == "auto_fit",
                "unknown aperture mode '" + ap.mode + "'");
    return ap;
}

/// Parses one sun, either the legacy `scene.sun` object or a `{"type":"sun", ...}` entry of
/// `scene.sources`. Only the latter may carry `type` and its own `aperture`; both accept
/// `wavelength_nm`, so the writer never has to change spelling just to record a wavelength.
SunSourceDoc parse_sun(const json& sj, bool strict, bool in_sources) {
    if (strict) {
        if (in_sources)
            reject_unknown_keys(sj, {"type", "direction", "dni_wm2", "sunshape", "azimuth_deg",
                                     "elevation_deg", "aperture", "wavelength_nm"}, "sun");
        else
            reject_unknown_keys(sj, {"direction", "dni_wm2", "sunshape", "azimuth_deg",
                                     "elevation_deg", "wavelength_nm"}, "sun");
    }
    require(sj.contains("sunshape"), "sun missing 'sunshape'");
    const json& shj = sj["sunshape"];
    if (strict)
        reject_unknown_keys(shj, {"type", "half_angle_mrad", "chi"}, "sunshape");

    SunSourceDoc sun;
    sun.sunshape_type = shj.value("type", std::string("pillbox"));
    sun.half_angle_mrad = shj.value("half_angle_mrad", 4.65);
    sun.chi = shj.value("chi", 0.05);
    sun.dni_wm2 = sj.value("dni_wm2", 1000.0);
    sun.wavelength_nm = sj.value("wavelength_nm", 550.0);
    if (strict)
        require(sun.sunshape_type == "pillbox" || sun.sunshape_type == "buie",
                "unknown sunshape type '" + sun.sunshape_type + "'");
    require(std::isfinite(sun.wavelength_nm) && sun.wavelength_nm > 0.0,
            "sun 'wavelength_nm' must be finite and > 0");

    if (sj.contains("direction")) {
        // Stored verbatim, un-normalized: build_scene() normalizes on the way into
        // SunSource. The angle fields are re-derived from the normalized direction so
        // they are never stale relative to it.
        math::vec3 d = read_vec3(sj, "direction");
        sun.direction = d;
        // Azimuth is degenerate at the pole, so pass any authored azimuth_deg as the
        // fallback. Without it a zenith sun round-trips to the default 180 and a
        // user's authored azimuth is silently lost on save/reload.
        const double fallback_az = sj.value("azimuth_deg", sun.azimuth_deg);
        sources::SunAngles angles = sources::SunSource::angles_from_direction(
            math::safe_normalize(d), fallback_az);
        sun.azimuth_deg = angles.azimuth_deg;
        sun.elevation_deg = angles.elevation_deg;
    } else {
        if (sj.contains("azimuth_deg"))
            sun.azimuth_deg = sj["azimuth_deg"].get<double>();
        if (sj.contains("elevation_deg"))
            sun.elevation_deg = sj["elevation_deg"].get<double>();
    }

    if (in_sources) {
        if (sj.contains("aperture"))
            sun.aperture = parse_aperture(sj["aperture"], strict);
        else
            sun.aperture.mode = "auto_fit";
    }
    return sun;
}

/// Parses a `{"type":"laser", ...}` entry of `scene.sources`. `origin` and `direction` are
/// required; the rest default to sources::Laser's own defaults.
LaserSourceDoc parse_laser(const json& lj, bool strict) {
    if (strict)
        reject_unknown_keys(lj, {"type", "origin", "direction", "power_w", "wavelength_nm",
                                 "beam_diameter_m", "divergence_mrad", "polarisation", "sampling",
                                 "coherence_length_m"}, "laser");
    LaserSourceDoc l;
    l.origin          = read_vec3(lj, "origin");
    l.direction       = read_vec3(lj, "direction");
    l.power_w         = lj.value("power_w", 1.0);
    l.wavelength_nm   = lj.value("wavelength_nm", 632.8);
    l.beam_diameter_m = lj.value("beam_diameter_m", 0.001);
    l.divergence_mrad = lj.value("divergence_mrad", 0.0);
    require(glm::dot(l.direction, l.direction) > 0.0, "laser 'direction' is zero-length");
    require(std::isfinite(l.power_w) && l.power_w >= 0.0, "laser 'power_w' must be >= 0");
    require(std::isfinite(l.wavelength_nm) && l.wavelength_nm > 0.0,
            "laser 'wavelength_nm' must be > 0");
    require(std::isfinite(l.beam_diameter_m) && l.beam_diameter_m >= 0.0,
            "laser 'beam_diameter_m' must be >= 0");
    require(std::isfinite(l.divergence_mrad) && l.divergence_mrad >= 0.0 &&
                l.divergence_mrad <= 1000.0 * math::PI,
            "laser 'divergence_mrad' is a full angle and must be within [0, 1000*pi]");
    if (lj.contains("polarisation")) {
        // Validated in both modes: a misspelt state would otherwise trace as unpolarised light and
        // quietly give the wrong answer for every polariser downstream.
        const auto& pj = lj["polarisation"];
        if (pj.is_string()) {
            const std::string s = pj.get<std::string>();
            require(s == "unpolarised" || s == "circular_left" || s == "circular_right",
                    "laser 'polarisation' must be \"unpolarised\", \"circular_left\", "
                    "\"circular_right\" or {\"linear_deg\": angle}; got \"" + s + "\"");
            l.polarisation = s;
        } else {
            require(pj.is_object() && pj.contains("linear_deg"),
                    "laser 'polarisation' object must be {\"linear_deg\": angle}");
            if (strict) reject_unknown_keys(pj, {"linear_deg"}, "laser.polarisation");
            l.polarisation            = "linear";
            l.polarisation_linear_deg = pj["linear_deg"].get<double>();
            require(std::isfinite(l.polarisation_linear_deg),
                    "laser 'polarisation.linear_deg' must be finite");
        }
    }
    if (lj.contains("sampling")) {
        l.sampling = lj["sampling"].get<std::string>();
        require(l.sampling == "random" || l.sampling == "grid",
                "laser 'sampling' must be \"random\" or \"grid\"; got \"" + l.sampling + "\"");
    }
    l.coherence_length_m = lj.value("coherence_length_m", 0.0);
    require(std::isfinite(l.coherence_length_m) && l.coherence_length_m >= 0.0,
            "laser 'coherence_length_m' must be >= 0 (0 = fully coherent)");
    return l;
}

// ---- parse_document -----------------------------------------------------

/// Parses a root `{"scene": {...}, "trace": {...}}` JSON document into a SceneDocument.
/// Does no filesystem work (mesh paths are stored unresolved); in strict mode, throws on any
/// unrecognized key at any level of the document.
SceneDocument parse_document(const json& root, bool strict) {
    if (strict)
        reject_unknown_keys(root, {"scene", "trace"}, "root");
    require(root.contains("scene"), "missing top-level 'scene' key");
    const json& s = root["scene"];
    if (strict)
        reject_unknown_keys(
            s, {"name", "sun", "aperture", "sources", "materials", "elements", "receiver"},
            "scene");

    SceneDocument doc;
    doc.name = s.value("name", std::string());

    // ---- Materials --------------------------------------------------------
    if (s.contains("materials")) {
        for (const auto& mj : s["materials"]) {
            require(mj.contains("id"), "material entry missing 'id'");
            require(mj.contains("type"), "material entry missing 'type'");
            MaterialDoc md;
            md.id = mj["id"].get<std::string>();
            md.type = mj["type"].get<std::string>();
            json params = mj;
            params.erase("id");
            params.erase("type");
            if (strict)
                validate_material_strict(md.type, params);
            md.params = std::move(params);
            doc.materials.push_back(std::move(md));
        }
    }

    // ---- Sources ----------------------------------------------------------
    // Two spellings: the general `sources` array, and the legacy `sun` (+ optional `aperture`)
    // pair, which desugars into one SunSourceDoc. Both at once is refused in BOTH modes: picking
    // a winner would silently drop an authored source, and this repo's history says silent-drop
    // bugs cost more than loud ones.
    require(!(s.contains("sources") && (s.contains("sun") || s.contains("aperture"))),
            "'sources' cannot be combined with the legacy 'sun'/'aperture' keys; put the sun "
            "(and its aperture) inside 'sources'");
    if (s.contains("sources")) {
        require(s["sources"].is_array(), "'sources' must be an array");
        for (const json& src : s["sources"]) {
            require(src.is_object() && src.contains("type"), "source entry missing 'type'");
            const std::string type = src["type"].get<std::string>();
            if (type == "sun")
                doc.sources.push_back(parse_sun(src, strict, /*in_sources=*/true));
            else if (type == "laser")
                doc.sources.push_back(parse_laser(src, strict));
            else
                throw std::runtime_error("SceneLoader: unknown source type '" + type + "'");
        }
    } else {
        require(s.contains("sun"), "missing 'sun' (or a 'sources' array)");
        SunSourceDoc sun = parse_sun(s["sun"], strict, /*in_sources=*/false);
        if (s.contains("aperture"))
            sun.aperture = parse_aperture(s["aperture"], strict);
        else
            sun.aperture.mode = "auto_fit";
        doc.sources.push_back(std::move(sun));
    }

    // ---- Elements ---------------------------------------------------------
    if (s.contains("elements")) {
        std::uint64_t next_id = 1;
        for (const auto& el : s["elements"]) {
            if (strict)
                reject_unknown_keys(el, {"name", "material", "surface", "transform", "visible",
                                         "body"},
                                     "element");
            require(el.contains("surface"), "element missing 'surface'");
            require(el.contains("material"), "element missing 'material'");

            ElementDoc ed;
            ed.id = next_id++;
            if (el.contains("name"))
                ed.name = el["name"].get<std::string>();
            ed.material_id = el["material"].get<std::string>();
            ed.surface = parse_surface_doc(el["surface"], strict);
            if (el.contains("transform"))
                ed.transform = parse_transform_doc(el["transform"], strict);
            ed.visible = el.value("visible", true);
            if (el.contains("body")) {
                // Validated in BOTH modes: these are cheap, and a negative slab or a cube that
                // is also a slab is a file that means nothing, not a key from the future.
                const auto& bj = el["body"];
                require(bj.is_object(), "element.body must be an object");
                if (strict)
                    reject_unknown_keys(bj, {"substrate_m", "cube", "post"}, "element.body");
                BodyDoc b;
                b.substrate_m = bj.value("substrate_m", 0.0);
                b.cube        = bj.value("cube", false);
                b.post        = bj.value("post", false);
                require(std::isfinite(b.substrate_m) && b.substrate_m >= 0.0,
                        "element.body.substrate_m must be >= 0");
                require(!(b.cube && b.substrate_m > 0.0),
                        "element.body: 'cube' and 'substrate_m' cannot both be set");
                ed.body = b;
            }
            doc.elements.push_back(std::move(ed));
        }
        doc.next_id = next_id;
    } else {
        doc.next_id = 1;
    }

    // ---- Receiver ---------------------------------------------------------
    require(s.contains("receiver"), "missing 'receiver'");
    {
        const json& rj = s["receiver"];
        if (strict)
            reject_unknown_keys(
                rj, {"surface", "grid", "transform", "depth", "top_mode", "type", "battery",
                     "coherent"},
                "receiver");

        ReceiverDoc rd;
        // The loader's else-branch takes every non-"box" value, including a typo — preserved.
        if (rj.value("type", std::string("plane")) == "box") {
            BoxReceiverDoc bd;
            if (rj.contains("surface")) {
                const json& surf = rj["surface"];
                if (strict)
                    reject_unknown_keys(surf, {"type", "half_width", "half_height"},
                                         "receiver.surface");
                bd.half_width = surf.value("half_width", bd.half_width);
                bd.half_height = surf.value("half_height", bd.half_height);
            }
            bd.depth = rj.value("depth", bd.depth);
            if (rj.contains("grid")) {
                const json& g = rj["grid"];
                if (strict)
                    reject_unknown_keys(g, {"nx", "ny"}, "receiver.grid");
                bd.nx = g.value("nx", bd.nx);
                bd.ny = g.value("ny", bd.ny);
            }
            if (rj.contains("battery")) {
                const json& bj = rj["battery"];
                if (strict)
                    reject_unknown_keys(bj,
                                         {"enabled", "half_width", "half_height", "height_m",
                                          "top_depth_m", "nx", "ny", "height"},
                                         "receiver.battery");
                BatteryDoc battery;
                battery.enabled = bj.value("enabled", true);
                if (bj.contains("half_width"))
                    battery.half_width = bj["half_width"].get<double>();
                if (bj.contains("half_height"))
                    battery.half_height = bj["half_height"].get<double>();
                if (bj.contains("top_depth_m"))
                    battery.top_depth_m = bj["top_depth_m"].get<double>();
                // Legacy alias: prefer "height_m", fall back to "height"; nullopt if neither.
                if (bj.contains("height_m"))
                    battery.height_m = bj["height_m"].get<double>();
                else if (bj.contains("height"))
                    battery.height_m = bj["height"].get<double>();
                if (bj.contains("nx"))
                    battery.nx = bj["nx"].get<int>();
                if (bj.contains("ny"))
                    battery.ny = bj["ny"].get<int>();
                bd.battery = std::move(battery);
            }
            rd.kind = std::move(bd);
        } else {
            PlaneReceiverDoc pd;
            if (rj.contains("grid")) {
                const json& g = rj["grid"];
                if (strict)
                    reject_unknown_keys(g, {"nx", "ny"}, "receiver.grid");
                pd.nx = g.value("nx", pd.nx);
                pd.ny = g.value("ny", pd.ny);
            }
            if (rj.contains("surface")) {
                const json& surf = rj["surface"];
                if (strict)
                    reject_unknown_keys(surf, {"type", "half_width", "half_height"},
                                         "receiver.surface");
                pd.half_width = surf.value("half_width", pd.half_width);
                pd.half_height = surf.value("half_height", pd.half_height);
            }
            rd.kind = std::move(pd);
        }
        if (rj.contains("transform"))
            rd.transform = parse_transform_doc(rj["transform"], strict);
        rd.coherent = rj.value("coherent", false);
        doc.receiver = std::move(rd);
    }

    // ---- Trace config -------------------------------------------------------
    {
        tracer::TraceConfig cfg;
        if (root.contains("trace")) {
            const json& tj = root["trace"];
            if (strict)
                reject_unknown_keys(tj,
                                     {"n_primary_rays", "max_bounces", "record_paths",
                                      "max_paths_to_record", "rng_seed", "power_cutoff_w"},
                                     "trace");
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
        doc.trace = cfg;
    }

    return doc;
}

} // namespace scrt::io
