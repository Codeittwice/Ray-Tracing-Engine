#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/RayRenderer.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/optics/Polarisation.hpp"

#include "scrt/surfaces/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Height the panel asks for when nothing is selected: enough for the prompt, and no more.
constexpr float kIdleHeight = 150.0f;
/// Height it asks for with a surface selected, which brings the whole transform editor with it.
constexpr float kEditHeight = 410.0f;
/// Height for a selected row that has no transform - the sun, a material, a receiver face.
constexpr float kInfoHeight = 210.0f;

/// What a clicked ray was doing along the clicked segment (Stage 7b).
void draw_ray_inspector(const tracer::RayEdgeInfo& e) {
    ImGui::TextDisabled("Ray segment");
    const glm::vec3 c = light_colour(e.wavelength_nm);
    ImGui::ColorButton("##ray_colour", ImVec4(c.r, c.g, c.b, 1.0f),
                       ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker);
    ImGui::SameLine();
    ImGui::Text("%.1f nm", e.wavelength_nm);

    if (e.power_w < 1e-3) ImGui::Text("Power carried: %.4g mW", e.power_w * 1e3);
    else                  ImGui::Text("Power carried: %.4g W", e.power_w);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("This one sampled ray's share of the source power along this segment -\n"
                          "not the beam's total, which is spread over every ray.");

    if (!e.polarised) {
        ImGui::Text("Polarisation: unpolarised");
    } else {
        // One pure function describes the state, shared with tests/test_ray_inspector.cpp, so what
        // this panel says is what the test checks.
        using K = optics::PolarisationDescription::Kind;
        const auto d = optics::describe_polarisation(e.Es, e.Ep, e.s_axis, e.direction);
        if (d.kind == K::Linear)
            ImGui::Text("Polarisation: linear, %.1f deg from vertical", d.axis_deg);
        else if (d.kind == K::Circular)
            ImGui::Text("Polarisation: circular, %s", d.ellipticity_deg > 0.0 ? "left" : "right");
        else
            ImGui::Text("Polarisation: elliptical, axis %.1f deg, ellipticity %.1f deg", d.axis_deg,
                        d.ellipticity_deg);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Angles are measured in the plane across the beam from its vertical, the\n"
                              "same reference a laser's linear_deg uses. Left circular turns counter-\n"
                              "clockwise seen looking INTO the beam. The white cross on the ray draws\n"
                              "the polarisation ellipse's axes.");
    }

    ImGui::Text("Optical path at segment end: %.6f m", e.opl_end_m);
    const double lambda_m = e.wavelength_nm * 1e-9;
    double       phase    = std::fmod(e.opl_end_m / lambda_m, 1.0) * 360.0;
    ImGui::Text("Phase there: %.1f deg", phase);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("2 pi x optical path / wavelength, modulo a full cycle. Two rays meeting with\n"
                          "phases 180 deg apart cancel on a coherent screen.");
    ImGui::TextDisabled("Click the background to clear.");
}

} // namespace

// ---- draw_selection_panel ------------------------------------------------------------------

float draw_selection_panel(PanelContext& ctx) {
    const SelectionInfo sel = current_selection();

    // Is there something the transform editor can actually place? A receiver face and a material
    // are selectable rows without a movable surface behind them, and promising a transform
    // editor for them and then showing nothing is worse than saying so.
    const bool placeable =
        sel.any && sel.surface_id != 0 && ctx.scene &&
        ctx.scene->surface_by_id(sel.surface_id) != nullptr;

    if (!sel.any && picked_ray_edge() >= 0) {
        draw_ray_inspector(*ray_edge(picked_ray_edge()));
    } else if (!sel.any) {
        ImGui::TextDisabled("Nothing selected");
        ImGui::Spacing();
        ImGui::TextWrapped("Click a part in the 3D view, or a row in the scene tree on the left, "
                           "to see what it is and where it sits.");
    } else {
        ImGui::TextDisabled("%s", sel.kind.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_CheckMark]);
        ImGui::Text("%s  %s", sel.icon, sel.label.c_str());
        ImGui::PopStyleColor();
        if (!sel.detail.empty()) ImGui::TextWrapped("%s", sel.detail.c_str());

        if (sel.any && !placeable && sel.surface_id == 0)
            ImGui::TextDisabled("This part has no placement of its own.");
    }

    // Always called, selected or not: ImGuizmo has to be primed every single frame, and it also
    // owns the mouse arbitration that hands the camera back when nothing is being dragged.
    draw_transform_panel(ctx);

    if (!sel.any && picked_ray_edge() >= 0) return kInfoHeight + 40.0f;   // the ray inspector's lines
    return !sel.any ? kIdleHeight : (placeable ? kEditHeight : kInfoHeight);
}

} // namespace scrt::viz
