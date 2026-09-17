#include "scrt/io/SceneWriter.hpp"
#include "scrt/io/Overloaded.hpp"
#include "scrt/sources/SunSource.hpp"
#include <variant>

namespace scrt::io {

using json = nlohmann::json;

namespace {

/// Serializes a math::vec3 as the 3-element JSON array read_vec3() expects.
json to_json(const math::vec3& v) {
    return json::array({v.x, v.y, v.z});
}

/// Flattens a math::mat4 (glm::dmat4, column-major storage) to 16 numbers in column-major
/// order: element k is m[k / 4][k % 4], i.e. columns 0..3 emitted in turn, matching
/// glm::value_ptr(m) memory order. No SceneLoader.cpp reader exists yet for this key; the
/// layout is documented here so a future reader can reconstruct the matrix unambiguously.
json matrix_to_json(const math::mat4& m) {
    json arr = json::array();
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            arr.push_back(m[col][row]);
    return arr;
}

/// Serializes a TransformDoc: translation/rotation always, scale only when non-identity,
/// matrix only when the explicit override is set.
json transform_to_json(const TransformDoc& t) {
    json j;
    j["translation"] = to_json(t.translation);
    j["rotation_euler_deg"] = to_json(t.rotation_euler_deg);
    if (t.scale != math::vec3(1.0))
        j["scale"] = to_json(t.scale);
    if (t.matrix.has_value())
        j["matrix"] = matrix_to_json(*t.matrix);
    return j;
}

/// Serializes one SurfaceDoc alternative to its SceneLoader.cpp `surface` JSON shape, including
/// the discriminating `type` key. One explicit lambda per alternative (see `overloaded` above)
/// so an unhandled future surface type is a compile error, not a silent omission.
json surface_to_json(const SurfaceDoc& sd) {
    return std::visit(
        overloaded{
            [](const PlaneDoc& p) {
                json j;
                j["type"] = "plane";
                j["half_width"] = p.half_width;
                j["half_height"] = p.half_height;
                return j;
            },
            [](const SphereDoc& sp) {
                json j;
                j["type"] = "sphere";
                j["radius"] = sp.radius;
                return j;
            },
            [](const ParaboloidDoc& pd) {
                json j;
                j["type"] = "paraboloid";
                j["focal_length_m"] = pd.focal_length_m;
                j["aperture_radius_m"] = pd.aperture_radius_m;
                return j;
            },
            [](const CylParaboloidDoc& cd) {
                json j;
                j["type"] = "cylindrical_paraboloid";
                j["focal_length_m"] = cd.focal_length_m;
                j["aperture_half_width_m"] = cd.aperture_half_width_m;
                j["aperture_half_length_m"] = cd.aperture_half_length_m;
                return j;
            },
            [](const QuadricDoc& qd) {
                json j;
                j["type"] = "quadric";
                j["coeffs"] = json{{"A", qd.A}, {"B", qd.B}, {"C", qd.C}, {"D", qd.D}, {"E", qd.E},
                                    {"F", qd.F}, {"G", qd.G}, {"H", qd.H}, {"I", qd.I}, {"J", qd.J}};
                j["aperture_box"] = json{{"min", to_json(qd.box_min)}, {"max", to_json(qd.box_max)}};
                return j;
            },
            [](const FresnelZoneLensDoc& fd) {
                json j;
                j["type"] = "fresnel_zone_lens";
                j["focal_length_m"] = fd.focal_length_m;
                j["inner_radius_m"] = fd.inner_radius_m;
                j["pitch_m"] = fd.pitch_m;
                j["n_zones"] = fd.n_zones;
                j["n_lens"] = fd.n_lens;
                return j;
            },
            [](const MeshDoc& md) {
                json j;
                j["type"] = "mesh";
                j["path"] = md.path;
                j["scale_to_meters"] = md.scale_to_meters;
                return j;
            },
            [](const ThickLensDoc& tl) {
                json j;
                j["type"] = "thick_lens";
                j["radius1"] = tl.radius1;
                j["radius2"] = tl.radius2;
                j["center_thickness_m"] = tl.center_thickness_m;
                j["diameter_m"] = tl.diameter_m;
                return j;
            },
            [](const DiskDoc& dd) {
                json j;
                j["type"] = "disk";
                j["radius"] = dd.radius;
                // Emitted only when it means something, so a solid disk stays two keys.
                if (dd.hole_radius > 0.0)
                    j["hole_radius"] = dd.hole_radius;
                return j;
            },
            [](const SlitPlateDoc& sp) {
                json j;
                j["type"] = "slit_plate";
                j["half_width"] = sp.half_width;
                j["half_height"] = sp.half_height;
                j["slit_width"] = sp.slit_width;
                j["slit_count"] = sp.slit_count;
                if (sp.slit_count > 1)
                    j["slit_pitch"] = sp.slit_pitch;
                return j;
            },
        },
        sd);
}

/// Serializes one ElementDoc. `id` is deliberately never written (session-local only);
/// `visible` is written only when false, so legacy files stay textually unchanged and a
/// hidden element is not silently lost.
///
/// An empty `name` is omitted rather than emitted as `""`. The two are NOT equivalent:
/// SceneLoader.cpp:263-264 calls set_name() only when the key is present, so an absent key
/// leaves the surface's own default name intact while `""` overwrites it with the empty
/// string. parse_document() leaves `name` empty exactly when the key was absent, so the
/// omission round-trips that state faithfully. Every one of the 10997 elements in the 959
/// shipped scene files carries a name, so this branch only fires for elements the editor
/// creates programmatically.
json element_to_json(const ElementDoc& el) {
    json j;
    if (!el.name.empty())
        j["name"] = el.name;
    j["material"] = el.material_id;
    j["surface"] = surface_to_json(el.surface);
    j["transform"] = transform_to_json(el.transform);
    if (!el.visible)
        j["visible"] = false;
    if (el.body) {
        // Only the fields that are set, so a hand-written body round-trips as written.
        json b = json::object();
        if (el.body->substrate_m > 0.0) b["substrate_m"] = el.body->substrate_m;
        if (el.body->cube)              b["cube"] = true;
        if (el.body->post)              b["post"] = true;
        j["body"] = std::move(b);
    }
    return j;
}

/// Serializes one MaterialDoc: `id`/`type` first, then every key of the raw `params` object
/// merged in verbatim (never allowed to overwrite `id`/`type`).
json material_to_json(const MaterialDoc& m) {
    json j;
    j["id"] = m.id;
    j["type"] = m.type;
    if (m.params.is_object()) {
        for (const auto& [key, value] : m.params.items()) {
            if (key == "id" || key == "type")
                continue;
            j[key] = value;
        }
    }
    return j;
}

/// Serializes the sun block. `direction` is always emitted (SceneLoader.cpp:238 requires it):
/// when the document holds an authored direction it wins and the angles are re-derived from it
/// (using the stored azimuth as the pole fallback); otherwise the stored angles are emitted
/// verbatim and the direction is derived from them.
json sun_to_json(const SunSourceDoc& sun) {
    json j;

    math::vec3 direction;
    double azimuth_deg;
    double elevation_deg;
    if (sun.direction.has_value()) {
        direction = *sun.direction;
        const auto angles = sources::SunSource::angles_from_direction(direction, sun.azimuth_deg);
        azimuth_deg = angles.azimuth_deg;
        elevation_deg = angles.elevation_deg;
    } else {
        azimuth_deg = sun.azimuth_deg;
        elevation_deg = sun.elevation_deg;
        direction = sources::SunSource::direction_from_angles({azimuth_deg, elevation_deg});
    }
    j["direction"] = to_json(direction);
    j["azimuth_deg"] = azimuth_deg;
    j["elevation_deg"] = elevation_deg;
    j["dni_wm2"] = sun.dni_wm2;

    json shape;
    shape["type"] = sun.sunshape_type;
    if (sun.sunshape_type == "pillbox") {
        shape["half_angle_mrad"] = sun.half_angle_mrad;
    } else if (sun.sunshape_type == "buie") {
        shape["chi"] = sun.chi;
    } else {
        // Unknown/future sunshape type: emit both so a round trip through an older/other
        // writer never silently drops either field.
        shape["half_angle_mrad"] = sun.half_angle_mrad;
        shape["chi"] = sun.chi;
    }
    j["sunshape"] = shape;
    // Emitted only when it means something: 550 is the engine's convention and core::Ray's
    // default, and writing it would change every shipped file on re-save.
    if (sun.wavelength_nm != 550.0)
        j["wavelength_nm"] = sun.wavelength_nm;
    return j;
}

/// Serializes the aperture block, including the constant `type: "disk"` (see ApertureDoc's
/// doc comment) and the Wave 1 `mode`/`margin` fields, which have no SceneLoader.cpp reader
/// yet but are emitted unconditionally per the cheap-and-lossless policy.
json aperture_to_json(const ApertureDoc& ap) {
    json j;
    j["type"] = "disk";
    j["center"] = to_json(ap.center);
    j["normal"] = to_json(ap.normal);
    j["radius"] = ap.radius;
    j["mode"] = ap.mode;
    j["margin"] = ap.margin;
    return j;
}

/// Serializes one entry of `scene.sources`, with its discriminating `type`. One lambda per
/// alternative and no catch-all (io::overloaded), so a third source type that reaches the
/// document but not this writer is a compile error here.
json source_to_json(const SourceDoc& sd) {
    return std::visit(
        overloaded{
            [](const SunSourceDoc& sun) {
                json j = sun_to_json(sun);
                j["type"] = "sun";
                j["aperture"] = aperture_to_json(sun.aperture);
                return j;
            },
            [](const LaserSourceDoc& l) {
                json j;
                j["type"] = "laser";
                j["origin"] = to_json(l.origin);
                j["direction"] = to_json(l.direction);
                j["power_w"] = l.power_w;
                j["wavelength_nm"] = l.wavelength_nm;
                j["beam_diameter_m"] = l.beam_diameter_m;
                j["divergence_mrad"] = l.divergence_mrad;
                // Omitted when unpolarised, so every existing laser scene re-saves unchanged.
                if (l.polarisation == "linear")
                    j["polarisation"] = json{{"linear_deg", l.polarisation_linear_deg}};
                else if (l.polarisation != "unpolarised")
                    j["polarisation"] = l.polarisation;
                if (l.sampling != "random") j["sampling"] = l.sampling;
                if (l.coherence_length_m > 0.0) j["coherence_length_m"] = l.coherence_length_m;
                return j;
            },
        },
        sd);
}

/// Serializes a BatteryDoc: `enabled` always, every other field only when its optional holds
/// a value (nullopt means "let the loader derive it from the enclosing box", and emitting a
/// derived number would bake it in as if the user had authored it).
json battery_to_json(const BatteryDoc& b) {
    json j;
    j["enabled"] = b.enabled;
    if (b.half_width.has_value())
        j["half_width"] = *b.half_width;
    if (b.half_height.has_value())
        j["half_height"] = *b.half_height;
    if (b.top_depth_m.has_value())
        j["top_depth_m"] = *b.top_depth_m;
    if (b.height_m.has_value())
        j["height_m"] = *b.height_m;
    if (b.nx.has_value())
        j["nx"] = *b.nx;
    if (b.ny.has_value())
        j["ny"] = *b.ny;
    return j;
}

/// Serializes the receiver block, plane or box. One explicit lambda per ReceiverDoc::kind
/// alternative (see `overloaded` above) so a future third receiver kind is a compile error.
/// `top_mode` is emitted only on the box branch — a plane receiver has no such concept and
/// SceneLoader.cpp never reads one there.
json receiver_to_json(const ReceiverDoc& recv) {
    json j = std::visit(
        overloaded{
            [](const PlaneReceiverDoc& p) {
                json rj;
                rj["type"] = "plane";
                rj["surface"] = json{{"half_width", p.half_width}, {"half_height", p.half_height}};
                rj["grid"] = json{{"nx", p.nx}, {"ny", p.ny}};
                return rj;
            },
            [](const BoxReceiverDoc& b) {
                json rj;
                rj["type"] = "box";
                rj["surface"] = json{{"half_width", b.half_width}, {"half_height", b.half_height}};
                rj["depth"] = b.depth;
                rj["top_mode"] = "record_pass";
                rj["grid"] = json{{"nx", b.nx}, {"ny", b.ny}};
                if (b.battery.has_value())
                    rj["battery"] = battery_to_json(*b.battery);
                return rj;
            },
        },
        recv.kind);
    j["transform"] = transform_to_json(recv.transform);
    if (recv.coherent) j["coherent"] = true;   // omitted when false: every existing file re-saves unchanged
    return j;
}

/// Serializes the root-level trace block. `TraceConfig::num_threads` has no SceneLoader.cpp
/// reader at all and is deliberately never emitted.
json trace_to_json(const tracer::TraceConfig& cfg) {
    json j;
    j["n_primary_rays"] = cfg.n_primary_rays;
    j["max_bounces"] = cfg.max_bounces;
    j["power_cutoff_w"] = cfg.power_cutoff_w;
    // Omitted at its default, so every existing file re-saves unchanged.
    if (cfg.power_cutoff_rel != tracer::TraceConfig{}.power_cutoff_rel) j["power_cutoff_rel"] = cfg.power_cutoff_rel;
    j["rng_seed"] = cfg.rng_seed;
    j["record_paths"] = cfg.record_paths;
    j["max_paths_to_record"] = cfg.max_paths_to_record;
    return j;
}

} // namespace

/// Serializes a SceneDocument back to the root `{"scene": {...}, "trace": {...}}` JSON shape
/// that SceneLoader.cpp / parse_document() consume. See the class-level report for the full
/// emit-vs-omit policy; in short: every required-by-loader key and every optional scalar key
/// is emitted unconditionally. The only omissions are ones where emitting would either change
/// the document's meaning or invent authorship: the session-local `ElementDoc::id`, an empty
/// `scene.name` or `element.name`, an identity `scale`, an unset `matrix`, a `visible` that is
/// true, unset `BatteryDoc` optionals, and a nullopt `battery`.
json write_document(const SceneDocument& doc) {
    json root;
    json& s = root["scene"];

    // Nothing reads scene.name (SceneLoader.cpp never looks at it; parse_document defaults it
    // to ""), so emitting `"name": ""` for a document that never had the key would be pure
    // diff noise with no meaning attached. All 959 shipped scene files do carry a name, so in
    // practice this always emits.
    if (!doc.name.empty())
        s["name"] = doc.name;

    json materials = json::array();
    for (const auto& m : doc.materials)
        materials.push_back(material_to_json(m));
    s["materials"] = std::move(materials);

    json elements = json::array();
    for (const auto& el : doc.elements)
        elements.push_back(element_to_json(el));
    s["elements"] = std::move(elements);

    s["receiver"] = receiver_to_json(doc.receiver);
    // Legacy-preferring, and load-bearing: exactly one sun and nothing else is written as the
    // `sun` + `aperture` pair every shipped file has, so all 94 re-save unchanged and the
    // assistant's system prompt (which teaches that shape) stays valid. Anything else - a laser,
    // two suns, no source at all - is the general `sources` array.
    if (doc.sources.size() == 1 && std::holds_alternative<SunSourceDoc>(doc.sources.front())) {
        const auto& sun = std::get<SunSourceDoc>(doc.sources.front());
        s["sun"] = sun_to_json(sun);
        s["aperture"] = aperture_to_json(sun.aperture);
    } else {
        json sources = json::array();
        for (const auto& sd : doc.sources)
            sources.push_back(source_to_json(sd));
        s["sources"] = std::move(sources);
    }

    root["trace"] = trace_to_json(doc.trace);

    return root;
}

} // namespace scrt::io
