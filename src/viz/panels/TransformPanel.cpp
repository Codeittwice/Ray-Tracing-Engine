#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/RayRenderer.hpp"
#include "scrt/viz/Theme.hpp"

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
    const math::mat4 world = world_matrix(st);

    // The document is authoritative for placement, so the edit goes through the editor
    // first. Writing Surface::set_transform directly - which this used to do - leaves the
    // TransformDoc holding the load-time transform, so moving an object and saving wrote
    // the OLD position back out. SceneEditor.hpp forbids exactly that.
    if (ctx.commit_transform) ctx.commit_transform(id, world);

    // Then the display. set_surface_transform re-uploads no geometry, so a drag costs no
    // re-tessellation; it rewrites the same matrix onto the surface, which is idempotent.
    RayRenderer(ctx.scene).set_surface_transform(id, core::Transform::from_matrix(world));
    // setTransform's own redraw request goes through updateStructureExtents, which returns
    // early while the extents are frozen (see set_extents_frozen), so ask explicitly.
    polyscope::requestRedraw();
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
    // Axis colours, the convention every 3D tool shares: X red, Y green, Z blue. They are
    // deliberately desaturated - three saturated bars in a row fight the amber accent that means
    // "selected" everywhere else in this interface.
    static const ImVec4 kAxis[3] = {
        ImVec4(0.78f, 0.30f, 0.32f, 1.0f),
        ImVec4(0.36f, 0.66f, 0.38f, 1.0f),
        ImVec4(0.33f, 0.52f, 0.80f, 1.0f),
    };
    static const char* kAxisId[3]   = {"##x", "##y", "##z"};
    static const char* kAxisName[3] = {"X", "Y", "Z"};

    bool changed = false;
    ImGui::PushID(label);

    // Label above the fields, not beside them. Beside, it was the first thing to be clipped
    // when the column narrowed, leaving three anonymous number boxes.
    ImGui::TextUnformatted(label);

    const ImGuiStyle& st = ImGui::GetStyle();
    const float sp    = st.ItemInnerSpacing.x;
    const float avail = ImGui::GetContentRegionAvail().x;
    const float width = std::max(1.0f, (avail - sp * 2.0f) / 3.0f);

    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine(0.0f, sp);
        ImGui::SetNextItemWidth(width);
        ImGui::BeginDisabled(lock[i]);
        ImGui::PushStyleColor(ImGuiCol_FrameBg,
                              ImVec4(kAxis[i].x, kAxis[i].y, kAxis[i].z, 0.22f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered,
                              ImVec4(kAxis[i].x, kAxis[i].y, kAxis[i].z, 0.34f));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive,
                              ImVec4(kAxis[i].x, kAxis[i].y, kAxis[i].z, 0.46f));
        if (ImGui::DragFloat(kAxisId[i], &v[i], speed, lo, hi, fmt)) changed = true;
        ImGui::PopStyleColor(3);
        ImGui::EndDisabled();

        // The axis letter, drawn into the field's left edge. A separate label would cost a
        // third of the row's width, and these rows are already the tightest thing on screen.
        const ImVec2 p0 = ImGui::GetItemRectMin();
        const ImVec2 p1 = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(p0.x + 4.0f, p0.y + (p1.y - p0.y - ImGui::GetFontSize()) * 0.5f),
            ImGui::GetColorU32(lock[i] ? st.Colors[ImGuiCol_TextDisabled] : kAxis[i]),
            kAxisName[i]);

        if (lock[i] && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("%s is locked. Clear the lock below to edit it.", kAxisName[i]);
    }
    ImGui::PopID();
    return changed;
}

/// Forces a scale triple back to uniform, keeping whichever axis moved most since `s0`.
///
/// `s0` MUST be the scale as it stood when the drag STARTED, never the previous frame's.
/// ImGuizmo's HandleScale writes absolute column lengths `mScale * mScaleValueOrigin`, where
/// mScaleValueOrigin is frozen at drag start and mScale stays exactly 1 on every axis the
/// user is not dragging - so each frame it puts the two idle axes back to their drag-start
/// lengths. Measured against the previous frame those idle axes therefore look *more*
/// changed than the dragged one the moment a collapse has raised them, this picks one of
/// them, and the whole triple snaps back to its starting value. The next frame picks the
/// dragged axis again. Against examples/panel_cooker.json that produced the sequence
/// 1.05, 1.10, 1.00, 1.20, 1.00, 1.30, 1.00 ... - a per-frame strobe between the dragged
/// size and the original one, which is what "scaling makes everything flash" actually was.
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

    // SCALE must be LOCAL. This ImGuizmo revision passes `mode` straight through to
    // ComputeContext (newer upstream forces LOCAL for scale there; this one does not), and in
    // WORLD mode ComputeContext sets mModel to the matrix's *translation only* - the linear
    // part is discarded. HandleScale then writes back `Scale(...) * mModel`, a matrix with no
    // rotation at all. Simulated over examples/panel_cooker.json's back_panel (base rotation
    // 67.5 deg about X), the first frame of a WORLD scale drag decomposes to
    // rot = (-67.5, 0, 0): the edit rotation becomes the exact inverse of the base rotation,
    // i.e. the object snaps flat. In LOCAL the same simulation holds rot at (0, 0, 0) for
    // every frame. The cost is that the handles follow the object's own axes rather than the
    // screen, which is correct local-space behaviour and what the tooltip below explains.
    const ImGuizmo::MODE space =
        (g_tool.op == GizmoOp::Scale) ? ImGuizmo::LOCAL
                                      : (g_tool.world ? ImGuizmo::WORLD : ImGuizmo::LOCAL);

    const bool moved = ImGuizmo::Manipulate(
        glm::value_ptr(view), glm::value_ptr(proj),
        static_cast<ImGuizmo::OPERATION>(mask), space, m);

    const bool using_now = ImGuizmo::IsUsing();
    set_mouse_grab(ImGuizmo::IsOver() || using_now);

    // The scale as it stood when this drag began. collapse_uniform needs a reference that does
    // NOT move under it every frame; see the note on that function for what using the previous
    // frame's triple instead did. Captured before any write-back, on the frame the drag starts.
    static bool  dragging = false;
    static float scale_at_drag_start[3] = {1.f, 1.f, 1.f};
    if (using_now && !dragging) std::copy(st.scale, st.scale + 3, scale_at_drag_start);
    dragging = using_now;

    // Only a live drag may write back. Widening the float matrix on an idle frame would
    // round-trip dmat4 -> mat4 -> dmat4 sixty times a second and slowly erode a transform
    // nobody is touching, so the buffer above is written for drawing and then discarded.
    if (!moved || !using_now) return false;

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

    // Each operation may only change its own components. ImGuizmo's LOCAL path orthonormalises
    // the model before applying a scale, which discards the scale already present and makes the
    // decomposed rotation of the result unreliable - in practice a scale drag was rewriting
    // Rotate to values like -129.6 deg and mirroring the object. Pinning the other two triples
    // to their drag-start values makes that impossible by construction rather than by trusting
    // the decomposition, and costs nothing: a drag that is scaling should not move or turn
    // anything anyway.
    switch (g_tool.op) {
        case GizmoOp::Scale:
            std::copy(t0, t0 + 3, st.trans);
            std::copy(r0, r0 + 3, st.rot_deg);
            break;
        case GizmoOp::Translate:
            std::copy(r0, r0 + 3, st.rot_deg);
            std::copy(s0, s0 + 3, st.scale);
            break;
        case GizmoOp::Rotate:
            std::copy(t0, t0 + 3, st.trans);
            std::copy(s0, s0 + 3, st.scale);
            break;
    }
    if (uniform_scale) collapse_uniform(st.scale, scale_at_drag_start);
    for (int i = 0; i < 3; ++i) st.scale[i] = std::max(kMinScale, st.scale[i]);
    return true;
}

/// Freezes Polyscope's global scene extents for the duration of an interactive edit.
///
/// Structure::setTransform recomputes state::lengthScale and state::boundingBox across EVERY
/// structure, and both feed things that are not the edited object: the ground plane's height
/// and tile size, and every length Polyscope stores as "relative" - including the ray-path
/// curve network's radius. Measured on examples/panel_cooker.json, driving one object's scale
/// between 1.0 and 2.5 swings lengthScale between 1.336 and 13.441, which redraws the ground
/// grid at ten times the size and turns the ray lines into fat tubes. That is the whole-scene
/// half of "everything flashes and scales together".
///
/// Scoped to the drag rather than frozen for the whole session: registration and runtime model
/// import still need the automatic sizing, and the extents are recomputed once on release.
void set_extents_frozen(bool frozen) {
    static bool frozen_now = false;
    if (frozen == frozen_now) return;
    frozen_now = frozen;
    polyscope::options::automaticallyComputeSceneExtents = !frozen;
    if (!frozen) polyscope::updateStructureExtents();  // catch up once, on release
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
    // The three tools as a segmented row of toggle buttons rather than radio dots: this is the
    // control the user reaches for most, and it should be a target, not a 13px circle.
    int         op    = static_cast<int>(g_tool.op);
    const float third = (ImGui::GetContentRegionAvail().x -
                         ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;
    struct Tool { const char* label; const char* tip; };
    static const Tool kTools[3] = {
        {ICON_FA_ARROWS_UP_DOWN_LEFT_RIGHT "  Move",  "Drag the arrows to move the object."},
        {ICON_FA_ROTATE "  Turn",                     "Drag a ring to rotate about the object."},
        {ICON_FA_UP_RIGHT_AND_DOWN_LEFT_FROM_CENTER "  Size",
                                                      "Drag a handle to resize the object."},
    };
    for (int i = 0; i < 3; ++i) {
        if (i) ImGui::SameLine();
        const bool on = (op == i);
        ImGui::BeginDisabled(i == 2 && support == surfaces::ScaleSupport::None);
        if (on) {
            // accent_fill, not the tick-mark colour: in the light theme that one is dark enough
            // to read as mud once it covers a whole button. The label colour is then chosen by
            // contrast against whatever fill the active theme gives us.
            const ImVec4 a = accent_fill();
            ImGui::PushStyleColor(ImGuiCol_Button, a);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, a);
            ImGui::PushStyleColor(ImGuiCol_Text, readable_on(a));
        }
        if (ImGui::Button(kTools[i].label, ImVec2(third, 0))) op = i;
        if (on) ImGui::PopStyleColor(3);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            if (i == 2 && support == surfaces::ScaleSupport::None)
                ImGui::SetTooltip("This surface has no size to change.");
            else
                ImGui::SetTooltip("%s", kTools[i].tip);
        }
    }
    g_tool.op = static_cast<GizmoOp>(op);

    ImGui::Checkbox("Show handles in the 3D view", &g_tool.enabled);

    // Scale is always local (see run_gizmo), so the choice is meaningless there rather than
    // merely unused - greying it out says so instead of silently ignoring the user.
    const bool scaling = (g_tool.op == GizmoOp::Scale);
    int space = (g_tool.world && !scaling) ? 1 : 0;
    ImGui::BeginDisabled(scaling);
    ImGui::RadioButton("Local", &space, 0); ImGui::SameLine();
    ImGui::RadioButton("World", &space, 1);
    ImGui::EndDisabled();
    if (scaling && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip(
            "Scaling is always about the object's own axes, so on a rotated object the\n"
            "scale handles are tilted with it rather than lined up with the screen.\n"
            "World-space scale is not offered because it cannot preserve the rotation.");
    if (!scaling) g_tool.world = (space == 1);

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
    //
    // No collapsing header: this draws inside the selection window in the right column, under
    // the read-out of what is selected, and appears only when there is something to place.
    if (st) {
        ImGui::Separator();
        ImGui::TextDisabled("Place this object");

        draw_tool_controls(support);
        ImGui::Spacing();

        // These fields and the gizmo write the very same triples, so they cannot disagree.
        // Click a field to type an exact value; drag it for fine adjustment.
        if (locked_drag3("Move (m)", st->trans, 0.005f, 0.0f, 0.0f, "%.4f", g_tool.lock))
            changed = true;
        if (locked_drag3("Turn (deg)", st->rot_deg, 0.5f, 0.0f, 0.0f, "%.2f", g_tool.lock))
            changed = true;
        if (draw_scale_controls(*st, support, uniform_scale)) changed = true;

        ImGui::Separator();
        if (draw_centre_controls(ctx, *st, *surf)) changed = true;

        if (ImGui::Button(ICON_FA_ARROW_ROTATE_LEFT "  Reset placement", ImVec2(-1, 0))) {
            for (int i = 0; i < 3; ++i) {
                st->trans[i]   = 0.0f;
                st->rot_deg[i] = 0.0f;
                st->scale[i]   = 1.0f;
            }
            changed = true;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Put the object back where the scene file had it.");
        ImGui::TextDisabled("Pivot: %.3f, %.3f, %.3f m", st->pivot.x, st->pivot.y, st->pivot.z);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("The point turns and resizes happen about. Taken from the\n"
                              "object's original position, so it never drifts as you edit.");
    }

    // Hold the global scene extents still for as long as the user is actually dragging
    // something - the gizmo, or one of the numeric fields above - and let them catch up on
    // release. ImGui::IsAnyItemActive() covers the DragFloat rows, which move a transform
    // continuously just like the gizmo does.
    set_extents_frozen(ImGuizmo::IsUsing() || ImGui::IsAnyItemActive());

    if (changed && st) apply_edit(ctx, id, *st);
}

} // namespace scrt::viz
