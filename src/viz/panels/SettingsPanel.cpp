#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Theme.hpp"

#include "polyscope/options.h"
#include "polyscope/polyscope.h"
#include "polyscope/view.h"

#include <array>

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Applies a theme to both the panels and the 3D viewport clear colour.
///
/// apply_theme() only touches ImGui; without the second half the 3D area keeps the other
/// theme's ground and the window reads as two applications sharing a screen.
void set_theme_and_viewport(Theme t) {
    apply_theme(t);
    polyscope::view::bgColor = viewport_background(t);
    polyscope::requestRedraw();
}

/// Tooltip on the item just submitted.
void tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

} // namespace

// ---- draw_settings_panel -------------------------------------------------------------------

void draw_settings_panel(PanelContext& ctx) {
    // The menu bar can ask for this panel. Consume the request here so it fires once and the
    // user can still collapse the panel again afterwards.
    if (ctx.settings_requested && *ctx.settings_requested) {
        *ctx.settings_requested = false;
        ImGui::SetNextItemOpen(true);
        ImGui::SetScrollHereY(1.0f);
    }
    if (!ImGui::CollapsingHeader("Settings###settings")) return;

    // ---- appearance ----
    ImGui::TextDisabled("Appearance");

    Theme      theme = current_theme();
    const bool dark  = (theme == Theme::Dark);
    if (ImGui::RadioButton("Dark", dark)) set_theme_and_viewport(Theme::Dark);
    tip("Darker panels, easier on the eyes for long sessions.");
    ImGui::SameLine();
    if (ImGui::RadioButton("Light", !dark)) set_theme_and_viewport(Theme::Light);
    tip("Lighter panels. Screenshots for reports and printed documentation read better on a\n"
        "light ground.");

    ImGui::Spacing();

    // ---- 3D view ----
    ImGui::TextDisabled("3D view");

    {
        bool ground = polyscope::options::groundPlaneMode != polyscope::GroundPlaneMode::None;
        if (ImGui::Checkbox("Show ground plane", &ground)) {
            polyscope::options::groundPlaneMode =
                ground ? polyscope::GroundPlaneMode::TileReflection
                       : polyscope::GroundPlaneMode::None;
        }
        tip("The shaded floor under the model. It is a depth cue, not part of the optics -\n"
            "no ray ever interacts with it.");
    }

    {
        // Height is pinned to Manual at load precisely so the floor stops tracking the scene
        // bounding box; a single reflector being scaled used to move it. Offering the slider
        // here keeps that adjustable without handing back the coupling.
        float h = polyscope::options::groundPlaneHeight;
        if (ImGui::DragFloat("Floor height (m)", &h, 0.01f, -50.0f, 50.0f, "%.3f")) {
            polyscope::options::groundPlaneHeight     = h;
            polyscope::options::groundPlaneHeightMode = polyscope::GroundPlaneHeightMode::Manual;
        }
        tip("Fixed height of the floor. It deliberately does NOT follow the model: letting it\n"
            "track the scene bounds made the whole background shift whenever an object was\n"
            "scaled.");
    }

    ImGui::Spacing();

    // ---- built-in panels ----
    ImGui::TextDisabled("Polyscope's own panels");

    {
        bool built_in = polyscope::options::buildDefaultGuiPanels;
        if (ImGui::Checkbox("Show built-in panels", &built_in))
            polyscope::options::buildDefaultGuiPanels = built_in;
        tip("Polyscope's own Structures / Selection / appearance windows.\n\n"
            "Hidden by default: they are positioned at fixed spots that sit on top of these\n"
            "panels. Turn them on to reach a built-in control this interface does not expose\n"
            "yet - per-structure colours, slice planes, screenshots - and expect them to\n"
            "overlap.");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("Window layout is fixed and follows the window size.\n"
                        "Positions are not saved between runs.");

    (void)ctx;
}

} // namespace scrt::viz
