#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <variant>
#include <vector>

// LoadedScene lives at the BOTTOM of this header, next to build_scene() which produces it, and
// SceneLoader.hpp includes this file rather than the reverse. It used to be the other way round,
// but LoadedScene now carries a SceneDocument by value, so the type must be complete where
// LoadedScene is defined; keeping the struct here is the only arrangement in which neither header
// needs the other's contents before its own.

namespace scrt::io {

/// One element's placement in world space: T * R(euler XYZ) * S applied to a local point.
///
/// Mirrors SceneLoader.cpp's `parse_transform` (translation + rotation_euler_deg only, composed
/// as T*R — see lines 49-61) plus two fields with **no SceneLoader.cpp call site today**: `scale`
/// and `matrix` are the JSON wiring for Wave 1's `core::Transform::from_scale`/`from_trs`/
/// `from_matrix`, added so the Wave 2+ gizmo/scale UI has somewhere to serialize to. A document
/// built from a legacy file always has `scale == {1,1,1}` and `matrix == std::nullopt`.
struct TransformDoc {
    math::vec3 translation{0};         ///< Metres. SceneLoader.cpp:56 (absent key -> {0,0,0}).
    math::vec3 rotation_euler_deg{0};  ///< Degrees, XYZ order. SceneLoader.cpp:52-53 (absent -> {0,0,0}).
    math::vec3 scale{1};               ///< New in Wave 2; no SceneLoader.cpp reader yet.
    std::optional<math::mat4> matrix;  ///< New in Wave 2; explicit 4x4 override, no reader yet.

    /// Builds the equivalent core::Transform; DECLARED ONLY, implemented in SceneDocument.cpp.
    core::Transform to_transform() const;
};

/// Finite rectangular plane surface. SceneLoader.cpp "plane" (lines 78-82).
struct PlaneDoc {
    double half_width{0.5};   ///< Metres. SceneLoader.cpp:79.
    double half_height{0.5};  ///< Metres. SceneLoader.cpp:80.
};

/// Sphere surface. SceneLoader.cpp "sphere" (lines 83-86).
struct SphereDoc {
    double radius{0.5};  ///< Metres. Required key in SceneLoader.cpp (no loader default);
                          ///< 0.5 here is only a placeholder for a freshly-inserted element.
};

/// Circular paraboloid mirror/dish. SceneLoader.cpp "paraboloid" (lines 87-93).
struct ParaboloidDoc {
    double focal_length_m{0.5};     ///< Required key, no loader default; placeholder value.
    double aperture_radius_m{0.5};  ///< Required key, no loader default; placeholder value.
};

/// Cylindrical (trough) paraboloid. SceneLoader.cpp "cylindrical_paraboloid" (lines 127-135).
struct CylParaboloidDoc {
    double focal_length_m{0.5};          ///< Required key, no loader default; placeholder value.
    double aperture_half_width_m{0.5};   ///< Required key, no loader default; placeholder value.
    double aperture_half_length_m{0.5};  ///< Required key, no loader default; placeholder value.
};

/// General implicit quadric Ax^2+By^2+Cz^2+Dxy+Exz+Fyz+Gx+Hy+Iz+J=0 clipped to a local AABB.
/// SceneLoader.cpp "quadric" (lines 94-113).
struct QuadricDoc {
    double A{0.0}, B{0.0}, C{0.0}, D{0.0}, E{0.0};  ///< Coeffs; each defaults 0.0 (SceneLoader.cpp:99-102).
    double F{0.0}, G{0.0}, H{0.0}, I{0.0}, J{0.0};  ///< Coeffs; each defaults 0.0 (SceneLoader.cpp:99-102).
    math::vec3 box_min{-1.0};  ///< Local clip AABB min. Required key, no loader default; placeholder.
    math::vec3 box_max{1.0};   ///< Local clip AABB max. Required key, no loader default; placeholder.
};

/// Fresnel zone lens (diffractive/refractive flat lens). SceneLoader.cpp "fresnel_zone_lens"
/// (lines 114-126).
struct FresnelZoneLensDoc {
    double focal_length_m{0.5};   ///< Required key, no loader default; placeholder value.
    double inner_radius_m{0.01};  ///< Required key, no loader default; placeholder value.
    double pitch_m{0.001};        ///< Required key, no loader default; placeholder value.
    int    n_zones{10};           ///< Required key, no loader default; placeholder value.
    double n_lens{1.5};           ///< Required key, no loader default; placeholder value.
};

/// Imported triangle mesh surface. SceneLoader.cpp "mesh" (lines 136-142).
struct MeshDoc {
    std::string path;             ///< Relative to the scene file's directory. Required key, no default.
    double      scale_to_meters{1.0};  ///< SceneLoader.cpp:138.
};

/// Discriminated union of every surface geometry SceneLoader.cpp can parse under `surface.type`.
using SurfaceDoc = std::variant<PlaneDoc, SphereDoc, ParaboloidDoc, CylParaboloidDoc,
                                 QuadricDoc, FresnelZoneLensDoc, MeshDoc>;

/// One placed, named, materialed piece of scene geometry. SceneLoader.cpp "elements" (lines
/// 255-273).
struct ElementDoc {
    std::uint64_t id{0};          ///< Session-local only; NEVER serialized. parse_document assigns
                                   ///< 1..N in document order so generated/hand-written files stay
                                   ///< diffable and merges never collide on id.
    std::string   name;           ///< SceneLoader.cpp:263-264 (absent key -> surface keeps its default name).
    std::string   material_id;    ///< SceneLoader.cpp:266 ("material" key); required.
    SurfaceDoc    surface;        ///< SceneLoader.cpp:260 ("surface" key); required.
    TransformDoc  transform;      ///< SceneLoader.cpp:261-262 (absent key -> identity transform).
    bool          visible{true};  ///< New in Wave 2 (editor outliner visibility). No SceneLoader.cpp
                                   ///< key exists today; every legacy element loads visible.
};

/// One named material with its full type-specific parameter set preserved verbatim.
///
/// `params` deliberately holds the raw JSON sub-object rather than a typed field per material
/// type. Material types are open-ended (`sellmeier` presets, `alpha_spectrum` point arrays — see
/// SceneLoader.cpp lines ~178-209) and round-tripping the object byte-for-byte is lossless for
/// free, including keys no material type has invented yet.
struct MaterialDoc {
    std::string id;    ///< SceneLoader.cpp:171; required.
    std::string type;  ///< SceneLoader.cpp:172; required ("perfect_mirror", "real_mirror",
                        ///< "dielectric", "thin_dielectric_pane", "absorber", ...).
    nlohmann::json params;  ///< Every other key in the material's JSON object, verbatim
                             ///< (e.g. reflectance, slope_error_mrad, n, absorption_per_m,
                             ///< sellmeier, alpha_spectrum, thickness_m).
};

/// Sun model: shape, irradiance, and position. SceneLoader.cpp "sun" (lines 222-241).
///
/// `azimuth_deg`/`elevation_deg` are the Wave 1 `sources::SunAngles` JSON wiring and have **no
/// SceneLoader.cpp call site today** — the loader still reads a raw `direction` vec3
/// (SceneLoader.cpp:238) and normalizes it. Their defaults (180, 90 = zenith) were chosen to
/// match `sources::SunAngles`'s own defaults, which map to propagation direction {0,0,-1}, the
/// direction every one of the 94 existing scenes uses.
struct SunDoc {
    std::string sunshape_type{"pillbox"};  ///< SceneLoader.cpp:226 ("sunshape.type").
    double      half_angle_mrad{4.65};     ///< Pillbox half-angle, mrad. SceneLoader.cpp:229.
    double      chi{0.05};                 ///< Buie circumsolar ratio. SceneLoader.cpp:232.
    double      dni_wm2{1000.0};           ///< Direct normal irradiance, W/m^2. SceneLoader.cpp:239.
    double      azimuth_deg{180.0};        ///< New in Wave 1/2; no SceneLoader.cpp reader yet.
    double      elevation_deg{90.0};       ///< New in Wave 1/2; no SceneLoader.cpp reader yet.

    /// Authored propagation direction, preserved verbatim whenever the JSON supplied one.
    ///
    /// `sun.direction` is a **required** key for SceneLoader.cpp (line 238 calls read_vec3, which
    /// throws when it is absent), so write_document() must always emit one. Re-deriving it from
    /// the angles instead would silently rewrite an authored vector: the round trip
    /// direction_from_angles(angles_from_direction(d)) reproduces `d` bitwise only near the pole,
    /// where SunSource.cpp:17-18 snaps the horizontal component to exactly 0. That is why all 41
    /// existing [0,0,-1] scenes survive a round trip unchanged, but any off-zenith vector — the
    /// very case Wave 1 added — comes back with different digits and a perturbed sampled ray
    /// sequence. When set, this field wins over azimuth_deg/elevation_deg; a UI that moves the sun
    /// by angle must clear it so the angles become authoritative again.
    std::optional<math::vec3> direction;
};

/// Collection aperture placement. SceneLoader.cpp "aperture" (lines 244-252).
///
/// `mode` mirrors `scene::ApertureMode` but is stored as a string (not the enum) for the same
/// forward-compatible-passthrough reason as `MaterialDoc::params`: it is new Wave 1 wiring with
/// **no SceneLoader.cpp call site today** (the loader always builds a fixed disk), so an
/// unrecognized future value should not fail to parse. `margin` is likewise unread by the loader.
///
/// The `aperture.type` key is deliberately **not** a field. All 41 shipped scene files carry
/// `"type": "disk"`, but SceneLoader.cpp never reads it and `scene::Aperture` models a disk and
/// nothing else (its area() is pi*r^2). Since the key has exactly one legal value it carries no
/// information to round-trip, so write_document() should emit `"type": "disk"` as a constant to
/// keep re-saved files textually close to the originals. Give it a field only when a second
/// aperture shape actually exists.
struct ApertureDoc {
    std::string mode{"fixed"};          ///< "fixed" or "auto_fit"; see scene::ApertureMode. Unused by loader today.
    math::vec3  center{0.0, 0.0, 2.0};  ///< World-space centre. Required key in SceneLoader.cpp (no
                                         ///< loader default); placeholder matches scene::Aperture's own default.
    math::vec3  normal{0.0, 0.0, 1.0};  ///< Unit normal toward the sun. Required key (no loader default);
                                         ///< placeholder matches scene::Aperture's own default.
    double      radius{1.0};            ///< Metres. SceneLoader.cpp:250.
    double      margin{0.05};           ///< Fractional slack for auto_fit. Unused by loader today.
};

/// Battery/inset absorber block nested inside a box receiver. SceneLoader.cpp lines 339-407.
///
/// Every field besides `enabled` has a SceneLoader.cpp default that is *derived* from sibling
/// values (the enclosing BoxReceiverDoc's own half-extents/depth/grid, or `depth - top_depth_m`)
/// rather than a literal, so it cannot be expressed as a fixed default in this struct. Each is
/// `std::optional`: `std::nullopt` means "key absent in JSON, parser must compute the
/// SceneLoader.cpp fallback"; a set value means the JSON key was present verbatim.
struct BatteryDoc {
    bool                  enabled{true};  ///< SceneLoader.cpp:341.
    std::optional<double> half_width;     ///< Default: enclosing box half_width * 0.5 (SceneLoader.cpp:343).
    std::optional<double> half_height;    ///< Default: enclosing box half_height * 0.5 (SceneLoader.cpp:344).
    std::optional<double> top_depth_m;    ///< Default: enclosing box depth (SceneLoader.cpp:345).
    std::optional<double> height_m;       ///< Populated from JSON key "height_m", falling back to the
                                           ///< legacy alias "height", falling back to `depth - top_depth_m`
                                           ///< (SceneLoader.cpp:346-347). nullopt means neither key was present.
    std::optional<int>    nx;             ///< Default: enclosing box grid nx (SceneLoader.cpp:352).
    std::optional<int>    ny;             ///< Default: enclosing box grid ny (SceneLoader.cpp:353).
};

/// Six-sided box receiver (top/bottom/four walls), optionally with a nested battery inset.
/// SceneLoader.cpp "receiver.type == box" (lines 279-420).
struct BoxReceiverDoc {
    double half_width{0.15};   ///< Metres. SceneLoader.cpp:280 (overridden by "surface.half_width").
    double half_height{0.15};  ///< Metres. SceneLoader.cpp:280 (overridden by "surface.half_height").
    double depth{0.15};        ///< Metres. SceneLoader.cpp:280,285 ("depth" key).
    int    nx{64};             ///< Grid columns. SceneLoader.cpp:287.
    int    ny{64};             ///< Grid rows. SceneLoader.cpp:287.
    std::optional<BatteryDoc> battery;  ///< Present only when the JSON "battery" object is present
                                         ///< (SceneLoader.cpp:339); nullopt means no battery face is emitted.
};

/// Single flat plane receiver. SceneLoader.cpp "receiver.type == plane", the default branch
/// (lines 421-442).
struct PlaneReceiverDoc {
    double half_width{0.05};   ///< Metres. SceneLoader.cpp:427.
    double half_height{0.05};  ///< Metres. SceneLoader.cpp:427.
    int    nx{64};             ///< Grid columns. SceneLoader.cpp:422.
    int    ny{64};             ///< Grid rows. SceneLoader.cpp:422.
};

/// A scene's single receiver, either a plane or a box; `type` is not stored, it is implied by
/// which alternative of `kind` is active (SceneLoader.cpp reads it only to pick a branch,
/// lines 279 and 421). `top_mode` is likewise not represented: the loader accepts only the
/// literal "record_pass" for a box receiver and throws on anything else (SceneLoader.cpp:409-411),
/// so the key currently carries no information to round-trip.
struct ReceiverDoc {
    std::variant<PlaneReceiverDoc, BoxReceiverDoc> kind;  ///< Defaults to PlaneReceiverDoc (variant's
                                                            ///< first alternative), matching the loader's
                                                            ///< `rj.value("type", "plane")` default.
    TransformDoc transform;  ///< SceneLoader.cpp:414-415 / 439-440 (absent key -> identity transform).
};

/// Root document: the full contents of one scene JSON file plus its trace configuration.
///
/// `name` is captured even though **SceneLoader.cpp reads it nowhere today** (Audit A1: "scene.name
/// is currently read by nothing") — captured here purely so a save/round-trip does not silently
/// drop authored text.
struct SceneDocument {
    std::string               name;              ///< Not read by SceneLoader.cpp; captured for round-tripping only.
    std::vector<MaterialDoc>  materials;          ///< SceneLoader.cpp:167-219 ("scene.materials"); may be empty.
    std::vector<ElementDoc>   elements;           ///< SceneLoader.cpp:255-273 ("scene.elements"); may be empty.
    ReceiverDoc                receiver;          ///< SceneLoader.cpp:276-443 ("scene.receiver"); required.
    SunDoc                     sun;                ///< SceneLoader.cpp:222-241 ("scene.sun"); required.
    ApertureDoc                aperture;           ///< SceneLoader.cpp:244-252 ("scene.aperture"); required.
    tracer::TraceConfig        trace;              ///< SceneLoader.cpp:446-461 (root "trace"); all keys optional,
                                                    ///< defaults come from tracer::TraceConfig itself. Note:
                                                    ///< TraceConfig::num_threads has no SceneLoader.cpp reader
                                                    ///< at all (missing from lines 449-460) — always its own
                                                    ///< struct default (0) until a JSON key is added.
    std::uint64_t              next_id{1};         ///< Session-local id allocator for new elements; not serialized.
};

/// Parses a root JSON document (the `{"scene": {...}, "trace": {...}}` shape SceneLoader.cpp
/// consumes) into a SceneDocument. When `strict` is true, unrecognized or malformed content
/// should be rejected rather than silently defaulted. DECLARED ONLY, implemented in
/// SceneDocument.cpp.
SceneDocument parse_document(const nlohmann::json& root, bool strict = false);

/// Serializes a SceneDocument back to the root JSON shape SceneLoader.cpp/parse_document consume.
/// DECLARED ONLY, implemented in SceneDocument.cpp.
nlohmann::json write_document(const SceneDocument& doc);

/// Live scene, trace configuration, and the SceneDocument all three were derived from.
///
/// `doc` is what makes save-on-load possible: every producer of a LoadedScene (build_scene, and
/// therefore load_scene) stores the document it built from, so a caller that loaded a file can
/// later write_document()/save_scene() it without re-reading and re-parsing the original. It is
/// the *authoritative* description of scene structure; `scene` is a derived view (see the sync
/// discipline documented on scene::SceneEditor).
struct LoadedScene {
    std::unique_ptr<scene::Scene> scene;  ///< Fully wired scene graph; null only on a default-
                                           ///< constructed LoadedScene.
    tracer::TraceConfig           cfg;    ///< Trace configuration, a copy of `doc.trace`.
    SceneDocument                 doc;    ///< The document `scene` was built from.
};

/// Builds a fully-wired LoadedScene (scene graph + trace config + a copy of `doc` itself) from a
/// SceneDocument, resolving any relative mesh paths against `base_dir`. Implemented in
/// SceneLoader.cpp.
LoadedScene build_scene(const SceneDocument& doc, const std::filesystem::path& base_dir);

/// Serializes doc via write_document() and writes the result to `path`. DECLARED ONLY,
/// implemented in SceneDocument.cpp.
void save_scene(const SceneDocument& doc, const std::filesystem::path& path);

} // namespace scrt::io
