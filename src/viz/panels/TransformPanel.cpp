#include "scrt/viz/Panels.hpp"
#include "scrt/viz/RayRenderer.hpp"

#include "scrt/core/AABB.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/surfaces/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "imgui.h"       // must precede ImGuizmo.h: ImGuizmo.h declares against ImGui types
#include "ImGuizmo.h"    // NOLINT(build/include_order)
#include "polyscope/polyscope.h"
#include "polyscope/view.h"
#include <glm/gtc/type_ptr.hpp>

namespace scrt::viz {

namespace {

// ---- gizmo tool state ---------------------------------------------------------
//
// This is tool state (which handle is showing, which axes are locked), not object data,
// so it lives with the panel and persists across frames — the same pattern the outliner
// uses for its selection. Per-object data lives in ObjectEditState instead.

/// Which manipulator handle the gizmo currently offers.
enum class GizmoOp { Translate = 0, Rotate = 1, Scale = 2 };

/// Persistent gizmo tool settings, shared by the 3D manipulator and the numeric fields.
struct GizmoTool {
    GizmoOp op        = GizmoOp::Translate;    ///< Active manipulator handle.
    bool    world     = true;                  ///< World axes when true, object-local when false.
    bool    lock[3]   = {false, false, false}; ///< Per-axis lock for X, Y, Z.
    bool    uniform   = true;                  ///< Uniform-scale lock (user preference).
    bool    enabled   = true;                  ///< Master switch for the 3D manipulator.
};

GizmoTool g_tool;                  ///< The one gizmo tool state (persists across frames).
bool      g_mouse_grabbed = false; ///< True while the camera has been handed to the gizmo.

/// Smallest scale factor the UI will produce; keeps the transform matrix invertible.
constexpr float kMinScale = 1e-3f;

// ---- float <-> double matrix boundary -----------------------------------------
//
// Everything upstream is double (glm::dmat4); ImGuizmo only speaks float[16] in the same
// column-major layout glm uses. These two functions are the only places the conversion
// happens, and from_f16 is only ever called on a frame where the gizmo is actually dragged.

/// Narrows a double matrix into ImGuizmo's column-major float[16].
void to_f16(const math::mat4& m, float* out) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            out[c * 4 + r] = static_cast<float>(m[c][r]);
}

/// Widens ImGuizmo's column-major float[16] back to double precision.
math::mat4 from_f16(const float* in) {
    math::mat4 m(1.0);
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            m[c][r] = static_cast<double>(in[c * 4 + r]);
    return m;
}

// ---- the edit matrix ----------------------------------------------------------

/// Pivot-local edit K = T(trans) * R(rot_deg) * S(scale), rebuilt from the widget triples.
math::mat4 edit_local(const ObjectEditState& st) {
    float k[16];
    // Recompose is the exact inverse of DecomposeMatrixToComponents, which is what the
    // gizmo write-back path uses, so the two input paths agree on the same triple.
    ImGuizmo::RecomposeMatrixFromComponents(st.trans, st.rot_deg, st.scale, k);
    return from_f16(k);
}

/// World matrix M = translate(pivot) * K * translate(-pivot) * base.
math::mat4 world_matrix(const ObjectEditState& st) {
    const math::mat4 p    = glm::translate(math::mat4(1.0), st.pivot);
    const math::mat4 pinv = glm::translate(math::mat4(1.0), -st.pivot);
    return p * edit_local(st) * pinv * st.base.matrix();
}

/// Inverse of world_matrix: recovers K from a world matrix the gizmo just produced.
math::mat4 local_from_world(const ObjectEditState& st, const math::mat4& world) {
    const math::mat4 p    = glm::translate(math::mat4(1.0), st.pivot);
    const math::mat4 pinv = glm::translate(math::mat4(1.0), -st.pivot);
    return pinv * world * st.base.inverse() * p;
}

/// World-space bounds centroid of a surface in its unedited base pose: the cached pivot.
math::vec3 base_pivot(const surfaces::Surface& surf, const core::Transform& base) {
    const core::AABB lb = surf.local_bounds();
    core::AABB box;
    // expand() sorts, so an inverted local AABB (negative focal length) still yields a
    // well-ordered world box here.
    for (double x : {lb.min().x, lb.max().x})
        for (double y : {lb.min().y, lb.max().y})
            for (double z : {lb.min().z, lb.max().z})
                box.expand(base.point_to_world({x, y, z}));
    return box.centroid();
}

/// Pushes the edit onto both the physics surface and its Polyscope structure.
void apply_edit(PanelContext& ctx, std::uint64_t id, const ObjectEditState& st) {
    if (!ctx.scene) return;
    // Placement-only edit: set_surface_transform writes the physics transform and the
    // structure's display transform from this one matrix and re-uploads no geometry, so a
    // drag costs no re-tessellation and the two can never diverge.
    RayRenderer(ctx.scene).set_surface_transform(
        id, core::Transform::from_matrix(world_matrix(st)));
    if (ctx.need_rebuild) *ctx.need_rebuild = true;
    if (ctx.need_retrace) *ctx.need_retrace = true;
}

// ---- mouse arbitration --------------------------------------------------------

/// Hands the mouse to the gizmo while it is hovered or dragged, and back to the camera after.
void set_mouse_grab(bool grab) {
    if (grab) ImGui::GetIO().WantCaptureMouse = true;
    if (grab == g_mouse_grabbed) return;
    g_mouse_grabbed = grab;
    // Polyscope samples this before invoking the user callback, so the flag set while merely
    // hovering is what suppresses the camera on the frame the drag actually starts.
    polyscope::state::doDefaultMouseInteraction = !grab;
}

// ---- widgets ------------------------------------------------------------------

/// Three-component drag row that greys out the components whose axis is locked.
bool locked_drag3(const char* label, float* v, float speed, float lo, float hi,
                  const char* fmt, const bool lock[3]) {
    static const char* kAxisId[3] = {"##x", "##y", "##z"};
    bool changed = false;
    ImGui::PushID(label);
    const float sp    = ImGui::GetStyle().ItemInnerSpacing.x;
    const float width = std::max(1.0f, (ImGui::CalcItemWidth() - sp * 2.0f) / 3.0f);
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0.0f, sp);
        ImGui::SetNextItemWidth(width);
        ImGui::BeginDisabled(lock[i]);
        if (ImGui::DragFloat(kAxisId[i], &v[i], speed, lo, hi, fmt)) changed = true;
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0.0f, sp);
    ImGui::TextUnformatted(label);
    ImGui::PopID();
    return changed;
}

/// Forces a scale triple back to uniform, keeping whichever axis the drag changed most.
void collapse_uniform(float* s, const float* s0) {
    int   best = 0;
    float dev  = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float ref = (std::fabs(s0[i]) > kMinScale) ? s0[i] : 1.0f;
        const float d   = std::fabs(s[i] / ref - 1.0f);
        if (d > dev) { dev = d; best = i; }
    }
    const float ref = (std::fabs(s0[best]) > kMinScale) ? s0[best] : 1.0f;
    const float k   = s[best] / ref;
    for (int i = 0; i < 3; ++i)
        s[i] = std::max(kMinScale, s0[i] * k);
}

// ---- the 3D manipulator -------------------------------------------------------

/// ImGuizmo operation mask for the active tool, with locked axes' handles removed.
unsigned gizmo_mask(surfaces::ScaleSupport support) {
    unsigned mask = 0;
    switch (g_tool.op) {
        case GizmoOp::Rotate:
            if (!g_tool.lock[0]) mask |= ImGuizmo::ROTATE_X;
            if (!g_tool.lock[1]) mask |= ImGuizmo::ROTATE_Y;
            if (!g_tool.lock[2]) mask |= ImGuizmo::ROTATE_Z;
            // The screen-space ring turns about an arbitrary axis, so it is only offered
            // when nothing is locked.
            if (!g_tool.lock[0] && !g_tool.lock[1] && !g_tool.lock[2])
                mask |= ImGuizmo::ROTATE_SCREEN;
            break;
        case GizmoOp::Scale:
            if (support == surfaces::ScaleSupport::None) break;
            if (!g_tool.lock[0]) mask |= ImGuizmo::SCALE_X;
            if (!g_tool.lock[1]) mask |= ImGuizmo::SCALE_Y;
            if (!g_tool.lock[2]) mask |= ImGuizmo::SCALE_Z;
            break;
        case GizmoOp::Translate:
        default:
            if (!g_tool.lock[0]) mask |= ImGuizmo::TRANSLATE_X;
            if (!g_tool.lock[1]) mask |= ImGuizmo::TRANSLATE_Y;
            if (!g_tool.lock[2]) mask |= ImGuizmo::TRANSLATE_Z;
            break;
    }
    return mask;
}

/// Draws the manipulator and folds a live drag back into the widget triples; true if moved.
bool run_gizmo(ObjectEditState& st, surfaces::ScaleSupport support, bool uniform_scale) {
    const unsigned mask = gizmo_mask(support);
    if (mask == 0) {                 // every axis locked: nothing to grab
        set_mouse_grab(false);
        return false;
    }

    const glm::mat4 view = polyscope::view::getCameraViewMatrix();
    const glm::mat4 proj = polyscope::view::getCameraPerspectiveMatrix();

    float m[16];
    to_f16(world_matrix(st), m);

    const bool moved = ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(proj),
        static_cast<ImGuizmo::OPERATION>(mask),
        g_tool.world ? ImGuizmo::WORLD : ImGuizmo::LOCAL, m);

    set_mouse_grab(ImGuizmo::IsOver() || ImGuizmo::IsUsing());

    // Only a live drag may write back. Widening the float matrix on an idle frame would
    // round-trip dmat4 -> mat4 -> dmat4 sixty times a second and slowly erode a transform
    // nobody is touching, so the buffer above is written for drawing and then discarded.
    if (!moved || !ImGuizmo::IsUsing()) return false;

    float t0[3], r0[3], s0[3];
    std::copy(st.trans, st.trans + 3, t0);
    std::copy(st.rot_deg, st.rot_deg + 3, r0);
    std::copy(st.scale, st.scale + 3, s0);

    float k[16];
    to_f16(local_from_world(st, from_f16(m)), k);
    ImGuizmo::DecomposeMatrixToComponents(k, st.trans, st.rot_deg, st.scale);

    // A locked axis must survive the round trip exactly, not merely to within float noise.
    for (int i = 0; i < 3; ++i) {
        if (!g_tool.lock[i]) continue;
        st.trans[i]   = t0[i];
        st.rot_deg[i] = r0[i];
        st.scale[i]   = s0[i];
    }
    if (uniform_scale) collapse_uniform(st.scale, s0);
    for (int i = 0; i < 3; ++i) st.scale[i] = std::max(kMinScale, st.scale[i]);
    return true;
}

// ---- centring helpers ---------------------------------------------------------

/// World-space centre of the receiver, unioned over all its faces; false when there is none.
bool receiver_centre(scene::Scene& sc, math::vec3& out) {
    auto* recv = sc.receiver();
    if (!recv) return false;
    const auto faces = recv->faces();
    if (faces.empty()) return false;
    core::AABB box;
    for (const auto& face : faces) {
        const auto b = face->surface()->world_bounds();
        box.expand(b.min());
        box.expand(b.max());
    }
    out = box.centroid();
    return true;
}

/// Sets the offset so the object's pivot lands on target along one axis; respects the lock.
void centre_axis(ObjectEditState& st, int axis, double target) {
    if (g_tool.lock[axis]) return;
    st.trans[axis] = static_cast<float>(target - st.pivot[axis]);
}

/// A centring button that is greyed out when its axis is locked; true when pressed.
bool centre_button(const char* label, int axis, float width) {
    ImGui::BeginDisabled(g_tool.lock[axis]);
    const bool hit = ImGui::Button(label, ImVec2(width, 0));
    ImGui::EndDisabled();
    return hit;
}

// ---- panel sections -----------------------------------------------------------

/// Draws the gizmo mode, space and axis-lock controls.
void draw_tool_controls(surfaces::ScaleSupport support) {
    ImGui::Checkbox("Show gizmo", &g_tool.enabled);

    int op = static_cast<int>(g_tool.op);
    ImGui::RadioButton("Move", &op, 0); ImGui::SameLine();
    ImGui::RadioButton("Rotate", &op, 1); ImGui::SameLine();
    ImGui::BeginDisabled(support == surfaces::ScaleSupport::None);
    ImGui::RadioButton("Scale", &op, 2);
    ImGui::EndDisabled();
    g_tool.op = static_cast<GizmoOp>(op);

    int space = g_tool.world ? 1 : 0;
    ImGui::RadioButton("Local", &space, 0); ImGui::SameLine();
    ImGui::RadioButton("World", &space, 1);
    g_tool.world = (space == 1);

    static const char* kLockTip =
        "A locked axis loses its gizmo handle and its numeric field.\n"
        "Lock X and Y to drag along Z only.";
    bool tip = false;
    ImGui::TextUnformatted("Lock axis");
    tip |= ImGui::IsItemHovered();
    ImGui::SameLine();
    ImGui::Checkbox("X##lock", &g_tool.lock[0]); tip |= ImGui::IsItemHovered(); ImGui::SameLine();
    ImGui::Checkbox("Y##lock", &g_tool.lock[1]); tip |= ImGui::IsItemHovered(); ImGui::SameLine();
    ImGui::Checkbox("Z##lock", &g_tool.lock[2]); tip |= ImGui::IsItemHovered();
    if (tip) ImGui::SetTooltip("%s", kLockTip);
}

/// Draws the scale row and the non-uniform unlock, honouring the surface's ScaleSupport.
bool draw_scale_controls(ObjectEditState& st, surfaces::ScaleSupport support,
                         bool uniform_scale) {
    if (support == surfaces::ScaleSupport::None) {
        ImGui::TextDisabled("This surface does not support scaling.");
        return false;
    }

    bool changed = false;
    if (uniform_scale) {
        const float mean = (st.scale[0] + st.scale[1] + st.scale[2]) / 3.0f;
        float       u    = mean;
        if (ImGui::DragFloat("Scale", &u, 0.005f, kMinScale, 1000.0f, "%.4f")) {
            // Scale the whole triple by the same ratio, so any existing (previously
            // unlocked) aspect ratio is preserved rather than flattened.
            const float k = (mean > kMinScale) ? (u / mean) : 1.0f;
            for (int i = 0; i < 3; ++i)
                st.scale[i] = std::max(kMinScale, st.scale[i] * k);
            changed = true;
        }
    } else if (locked_drag3("Scale", st.scale, 0.005f, kMinScale, 1000.0f, "%.4f",
                            g_tool.lock)) {
        changed = true;
    }

    const bool forced     = (support != surfaces::ScaleSupport::Free);
    bool       nonuniform = !uniform_scale;
    ImGui::BeginDisabled(forced);
    if (ImGui::Checkbox("Unlock non-uniform scale", &nonuniform))
        g_tool.uniform = !nonuniform;
    ImGui::EndDisabled();
    if (forced && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(
            "This surface is defined by a focal length, so only uniform scale is offered.\n"
            "A non-uniformly scaled paraboloid still traces, but it is no longer a\n"
            "paraboloid and has no focal point, while the parameter panel would still be\n"
            "showing focal_length_m.");
    }
    return changed;
}

/// Draws the centring shortcuts; true when one of them changed the offset.
bool draw_centre_controls(PanelContext& ctx, ObjectEditState& st,
                          const surfaces::Surface& surf) {
    bool        changed = false;
    const float avail   = ImGui::GetContentRegionAvail().x;
    const float gap     = ImGui::GetStyle().ItemSpacing.x;
    const float w3      = std::max(1.0f, (avail - gap * 2.0f) / 3.0f);
    const float w2      = std::max(1.0f, (avail - gap) / 2.0f);

    if (centre_button("Centre X", 0, w3)) { centre_axis(st, 0, 0.0); changed = true; }
    ImGui::SameLine();
    if (centre_button("Centre Y", 1, w3)) { centre_axis(st, 1, 0.0); changed = true; }
    ImGui::SameLine();
    if (centre_button("Centre Z", 2, w3)) { centre_axis(st, 2, 0.0); changed = true; }

    if (ImGui::Button("Centre to origin", ImVec2(w2, 0))) {
        for (int i = 0; i < 3; ++i) centre_axis(st, i, 0.0);  // skips locked axes
        changed = true;
    }
    ImGui::SameLine();
    if (centre_button("Drop to Z=0", 2, w2)) {
        // Uses the surface's *current* world bounds, so the object rests on the ground
        // plane whatever rotation and scale are already applied.
        st.trans[2] -= static_cast<float>(surf.world_bounds().min().z);
        changed = true;
    }

    math::vec3 rc{};
    const bool have_recv = ctx.scene && receiver_centre(*ctx.scene, rc);
    ImGui::BeginDisabled(!have_recv);
    if (ImGui::Button("Align to receiver (XY)", ImVec2(-1, 0))) {
        centre_axis(st, 0, rc.x);
        centre_axis(st, 1, rc.y);
        changed = true;
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(have_recv
            ? "Puts the object's centre under the receiver centre in X and Y.\n"
              "Height (Z) is left alone, so a reflector keeps its stand-off."
            : "The scene has no receiver.");
    }
    return changed;
}

} // namespace

/// Draws the per-object transform editor panel and the 3D manipulator gizmo.
void draw_transform_panel(PanelContext& ctx) {
    // ImGuizmo must be primed every frame, before anything can call Manipulate, and
    // regardless of whether the panel below is open or anything is selected — otherwise it
    // would reuse the previous frame's draw list. This function runs inside the polyscope
    // user callback, which is where ImGuizmo wants these calls.
    ImGuizmo::SetImGuiContext(ImGui::GetCurrentContext());
    ImGuizmo::BeginFrame();
    const ImGuiIO& io = ImGui::GetIO();
    ImGuizmo::SetRect(0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
    ImGuizmo::SetOrthographic(polyscope::view::projectionMode
                              == polyscope::ProjectionMode::Orthographic);

    // ---- resolve the outliner's selection --
    std::uint64_t      id   = 0;
    surfaces::Surface* surf = nullptr;
    ObjectEditState*   st   = nullptr;
    if (ctx.scene && ctx.edits && ctx.selected_id) {
        id = *ctx.selected_id;
        if (id != 0) surf = ctx.scene->surface_by_id(id);
        if (surf) {
            auto it = ctx.edits->find(id);
            if (it != ctx.edits->end()) st = &it->second;
        }
    }
    if (!st) surf = nullptr;

    // ---- pivot: computed once per object, then cached --
    // Recomputing this per frame would let the pivot chase the object as it moves, which
    // makes a rotation drift instead of spinning in place. It is derived from the *base*
    // pose, so it is also independent of the current edit and never jumps on re-selection.
    if (st && !st->pivot_valid) {
        st->pivot       = base_pivot(*surf, st->base);
        st->pivot_valid = true;
    }

    const surfaces::ScaleSupport support =
        surf ? surf->scale_support() : surfaces::ScaleSupport::Free;
    // UniformOnly and None surfaces are held to uniform scale without touching the user's
    // own preference, so it comes back when a freely scalable object is selected again.
    const bool uniform_scale = g_tool.uniform || (support != surfaces::ScaleSupport::Free);
    // A surface that cannot be scaled at all has no scale handle to offer, so fall back to
    // the move tool rather than leaving the user with an inert gizmo.
    if (support == surfaces::ScaleSupport::None && g_tool.op == GizmoOp::Scale)
        g_tool.op = GizmoOp::Translate;

    bool changed = false;
    if (st && g_tool.enabled) changed = run_gizmo(*st, support, uniform_scale);
    else                      set_mouse_grab(false);

    // ---- panel --
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!st) {
            ImGui::TextDisabled("Select an object in the Outliner to place it.");
        } else {
            const std::string name =
                surf->name().empty() ? "surface_" + std::to_string(id) : surf->name();
            ImGui::Text("%s", name.c_str());

            draw_tool_controls(support);
            ImGui::Separator();

            // These fields and the gizmo write the very same triples, so they cannot disagree.
            if (locked_drag3("Translate (m)", st->trans, 0.005f, 0.0f, 0.0f, "%.4f",
                             g_tool.lock))
                changed = true;
            if (locked_drag3("Rotate (deg)", st->rot_deg, 0.5f, 0.0f, 0.0f, "%.2f",
                             g_tool.lock))
                changed = true;
            if (draw_scale_controls(*st, support, uniform_scale)) changed = true;

            ImGui::Separator();
            if (draw_centre_controls(ctx, *st, *surf)) changed = true;

            if (ImGui::Button("Reset transform", ImVec2(-1, 0))) {
                for (int i = 0; i < 3; ++i) {
                    st->trans[i]   = 0.0f;
                    st->rot_deg[i] = 0.0f;
                    st->scale[i]   = 1.0f;
                }
                changed = true;
            }
            ImGui::TextDisabled("Pivot: %.3f, %.3f, %.3f m",
                                st->pivot.x, st->pivot.y, st->pivot.z);
        }
    }

    if (changed && st) apply_edit(ctx, id, *st);
}

} // namespace scrt::viz
