#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/Fonts.hpp"
#include "scrt/viz/Align.hpp"
#include "scrt/viz/RayRenderer.hpp"
#include "scrt/viz/Theme.hpp"
#include "scrt/viz/ViewSettings.hpp"

#include "polyscope/options.h"
#include "polyscope/polyscope.h"
#include "polyscope/view.h"

#include <array>
#include <cfloat>

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

// ---- draw_settings_window ------------------------------------------------------------------

void draw_settings_window(PanelContext& ctx) {
    if (!ctx.settings_open || !*ctx.settings_open) return;

    // Floating and movable, unlike every other window here: it is opened from the top bar's
    // gear, has nothing to do with the scene being edited, and is closed again immediately.
    // Centred on first appearance only, so dragging it somewhere sticks for the session.
    const ImVec2 centre(ImGui::GetIO().DisplaySize.x * 0.5f, ImGui::GetIO().DisplaySize.y * 0.5f);
    ImGui::SetNextWindowPos(centre, ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
    // Width is CONSTRAINED, not just initialised. With AlwaysAutoResize alone the window fits its
    // content while the sliders and the Close button (width -1) fit the window, and the two chase
    // each other down over a dozen frames: the panel visibly opened wide and shrank into place.
    const float w = 560.0f * ui_scale();
    ImGui::SetNextWindowSizeConstraints(ImVec2(w, 0.0f), ImVec2(w, FLT_MAX));

    if (!ImGui::Begin(ICON_FA_GEAR "  Settings", ctx.settings_open,
                      ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    // ---- appearance ----
    ImGui::TextDisabled("Appearance");

    Theme      theme = current_theme();
    const bool dark  = (theme == Theme::Dark);
    if (ImGui::RadioButton(ICON_FA_MOON "  Dark", dark)) set_theme_and_viewport(Theme::Dark);
    tip("Darker panels, easier on the eyes for long sessions.");
    ImGui::SameLine();
    if (ImGui::RadioButton(ICON_FA_SUN "  Light", !dark)) set_theme_and_viewport(Theme::Light);
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

    // ---- flux map ----
    ImGui::TextDisabled("Flux map");

    {
        const auto& names = flux_colormap_names();
        int         current = 0;
        for (std::size_t i = 0; i < names.size(); ++i)
            if (names[i] == flux_colormap()) current = static_cast<int>(i);

        if (ImGui::BeginCombo("Colours", flux_colormap_label(names[static_cast<std::size_t>(current)]))) {
            for (std::size_t i = 0; i < names.size(); ++i) {
                const bool sel = (static_cast<int>(i) == current);
                if (ImGui::Selectable(flux_colormap_label(names[i]), sel)) {
                    set_flux_colormap(names[i]);
                    // The receiver mesh carries the colormap on its scalar quantity, so it only
                    // changes on the next registration - which the retrace flag brings about.
                    if (ctx.need_rebuild) *ctx.need_rebuild = true;
                }
                if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        tip("Applies to both the heatmap in the analysis panel and the receiver in the 3D\n"
            "view - they are sampled from the same ramp, so they always agree.");
    }

    {
        float sigma = flux_smoothing_sigma();
        if (ImGui::SliderFloat("Smoothing", &sigma, 0.0f, 4.0f,
                               sigma <= 0.0f ? "off" : "%.1f bins")) {
            set_flux_smoothing_sigma(sigma);
            if (ctx.need_rebuild) *ctx.need_rebuild = true;
        }
        tip("Gaussian blur over the flux map, in receiver bins.\n\n"
            "The speckle is Poisson noise - with few rays per bin, neighbouring bins differ by\n"
            "chance rather than by physics. Blurring makes the shape of the hot spot legible.\n\n"
            "It changes the PICTURE only. The hottest-spot figure, the CSV and the JSON summary\n"
            "are all still computed from the raw bins, and a blur would lower the peak.\n"
            "More rays is the real fix; this makes what you have readable.");
    }

    ImGui::Spacing();

    // ---- ray paths ----
    ImGui::TextDisabled("Ray paths");

    {
        float r_mm = ray_radius_m() * 1000.0f;
        if (ImGui::SliderFloat("Ray thickness", &r_mm, 0.2f, 20.0f, "%.1f mm",
                               ImGuiSliderFlags_Logarithmic)) {
            set_ray_radius_m(r_mm / 1000.0f);
            apply_ray_appearance();
        }
        tip("Thickness of the drawn light paths, in millimetres.\n"
            "Absolute, so it does not change when an object is scaled.");
    }

    {
        float a = ray_opacity();
        if (ImGui::SliderFloat("Ray opacity", &a, 0.05f, 1.0f, "%.2f")) {
            set_ray_opacity(a);
            apply_ray_appearance();
        }
        tip("Turn this down when the rays hide the cooker they are bouncing off.");
    }

    ImGui::Spacing();

    // ---- placement grid ----
    ImGui::TextDisabled("Placement grid");
    {
        bool on = show_grid();
        if (ImGui::Checkbox("Show grid", &on)) set_show_grid(on);
        tip("Lines at the move-snap step (Place this object > Snap to steps), coarsened when\n"
            "they would be too dense. Drawn at z = 0 when the scene reaches the floor, and only\n"
            "across the scene's own extent, so it never changes the view's sense of scale.");
    }

    {
        bool on = show_posts();
        if (ImGui::Checkbox("Show posts", &on)) {
            set_show_posts(on);
            if (ctx.scene) RayRenderer(ctx.scene).sync_bodies();
        }
        tip("The black posts drawn under bench parts. Drawn only - no ray ever meets one -\n"
            "so hiding them changes the picture and nothing else.");
    }

    {
        bool on = align_axis().show;
        if (ImGui::Checkbox("Show alignment axis", &on)) align_axis().show = on;
        tip("The orange line that \"Centre on axis\" and \"Face along axis\" (Place this object)\n"
            "line parts up with. It starts on the first laser's beam.");
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

    ImGui::Spacing();
    if (ImGui::Button(ICON_FA_XMARK "  Close", ImVec2(-1, 0))) *ctx.settings_open = false;

    ImGui::End();
}

} // namespace scrt::viz
