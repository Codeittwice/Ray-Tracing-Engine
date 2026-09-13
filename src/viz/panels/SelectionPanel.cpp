#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Icons.hpp"

#include "scrt/surfaces/Surface.hpp"

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

    if (!sel.any) {
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

    return !sel.any ? kIdleHeight : (placeable ? kEditHeight : kInfoHeight);
}

} // namespace scrt::viz
