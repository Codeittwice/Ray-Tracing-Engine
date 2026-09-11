#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Theme.hpp"

#include "polyscope/polyscope.h"
#include "polyscope/view.h"

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Menu entries that are agreed in shape but not yet wired to anything.
///
/// They are drawn disabled rather than omitted so the shape of the application is visible while
/// we decide what belongs up here - and so nothing appears to work when it does not. Replace the
/// disabled item with a real one as each lands; do not enable a stub.
void placeholder(const char* label, const char* shortcut = nullptr) {
    ImGui::BeginDisabled();
    ImGui::MenuItem(label, shortcut);
    ImGui::EndDisabled();
}

} // namespace

// ---- draw_top_bar --------------------------------------------------------------------------

float draw_top_bar(PanelContext& ctx) {
    float height = 0.0f;
    if (!ImGui::BeginMainMenuBar()) return height;

    if (ImGui::BeginMenu("File")) {
        placeholder("New scene", "Ctrl+N");
        placeholder("Open...", "Ctrl+O");
        placeholder("Open recent");
        ImGui::Separator();
        placeholder("Save", "Ctrl+S");
        placeholder("Save as...", "Ctrl+Shift+S");
        placeholder("Revert");
        ImGui::Separator();
        placeholder("Exit");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Project")) {
        placeholder("Scene properties");
        placeholder("Materials library");
        placeholder("Batch compare...");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Import / Export")) {
        placeholder("Import 3D model...");
        placeholder("Import scene JSON...");
        ImGui::Separator();
        placeholder("Export flux map (CSV)");
        placeholder("Export summary (JSON)");
        placeholder("Export scene (OBJ)");
        placeholder("Save screenshot");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        placeholder("Frame scene");
        placeholder("Top / Front / Side");
        ImGui::Separator();
        placeholder("Show rays");
        placeholder("Show aperture");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        placeholder("Quick start");
        placeholder("Scene file reference");
        placeholder("About");
        ImGui::EndMenu();
    }

    // Right-hand cluster: account, theme, settings. Laid out right-to-left from the window
    // edge so adding or removing one does not shift the others.
    {
        const ImGuiStyle& st   = ImGui::GetStyle();
        const bool        dark = (current_theme() == Theme::Dark);

        // The theme toggle names the theme you would switch TO, not the one you are in - a
        // button labelled with the current state reads as a status display, not a control.
        const char* theme_lbl = dark ? "Light" : "Dark";
        const char* acct_lbl  = "Account";
        const char* set_lbl   = "Settings";

        auto item_w = [&](const char* t) {
            return ImGui::CalcTextSize(t).x + st.ItemSpacing.x * 2.0f;
        };
        const float total = item_w(acct_lbl) + item_w(theme_lbl) + item_w(set_lbl);
        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - total - st.WindowPadding.x);

        ImGui::BeginDisabled();
        ImGui::MenuItem(acct_lbl);
        ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Sign-in, for the scene assistant's API key.\n\n"
                              "Not yet implemented.");

        if (ImGui::MenuItem(theme_lbl)) {
            const Theme next = dark ? Theme::Light : Theme::Dark;
            apply_theme(next);
            polyscope::view::bgColor = viewport_background(next);  // panels and 3D together
            polyscope::requestRedraw();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Switch to the %s theme.", dark ? "light" : "dark");

        if (ImGui::MenuItem(set_lbl) && ctx.open_settings) ctx.open_settings();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Appearance, 3D view and built-in panels.");
    }

    height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
    return height;
}

// ---- draw_viewport_buttons -----------------------------------------------------------------

void draw_viewport_buttons(PanelContext& ctx, ImVec2 viewport_min, ImVec2 viewport_max) {
    if (viewport_max.x <= viewport_min.x) return;

    constexpr float kBtn    = 44.0f;
    constexpr float kGap    = 9.0f;
    constexpr float kInset  = 16.0f;
    const int       kCount  = 2;

    const float w = kBtn + kInset;
    const float h = kCount * kBtn + (kCount - 1) * kGap + kInset;

    ImGui::SetNextWindowPos(ImVec2(viewport_max.x - w, viewport_max.y - h), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(w, h), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);   // the buttons float; the window itself is invisible

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_AlwaysAutoResize;

    if (!ImGui::Begin("##viewport_buttons", nullptr, flags)) { ImGui::End(); return; }

    ImGuiStyle& st       = ImGui::GetStyle();
    const float saved    = st.FrameRounding;
    st.FrameRounding     = kBtn * 0.5f;   // circular

    // Assistant. Accent-filled so it reads as the primary action of the pair.
    ImGui::PushStyleColor(ImGuiCol_Button,        st.Colors[ImGuiCol_CheckMark]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, st.Colors[ImGuiCol_SeparatorHovered]);
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.10f, 0.07f, 0.02f, 1.0f));
    ImGui::Button("AI", ImVec2(kBtn, kBtn));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scene assistant - describe a cooker, attach reference photos and\n"
                          "specification documents, and have a scene generated.\n\n"
                          "Not yet implemented.");

    // Import / export. One button, since they are the same dialog in two directions.
    ImGui::Button("<>", ImVec2(kBtn, kBtn));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a 3D model, or export the scene and its results.\n\n"
                          "Not yet implemented - use the Import panel on the left for now.");

    st.FrameRounding = saved;
    ImGui::End();
    (void)ctx;
}

} // namespace scrt::viz
