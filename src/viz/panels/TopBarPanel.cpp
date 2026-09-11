#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Theme.hpp"

#include "polyscope/polyscope.h"
#include "polyscope/view.h"

#include "imgui.h"

#include <cmath>

namespace scrt::viz {

namespace {

/// Draws a sun glyph centred in `c`, radius `r`, using the draw list.
///
/// Drawn rather than typed: the bundled font is Lato, which has no sun, moon, gear or person
/// glyph, so a Unicode character would render as an empty box on every machine.
void glyph_sun(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddCircle(c, r * 0.50f, col, 0, 1.6f);
    for (int i = 0; i < 8; ++i) {
        const float a  = static_cast<float>(i) * 0.7853981f;   // 45 degrees
        const ImVec2 p0(c.x + std::cos(a) * r * 0.72f, c.y + std::sin(a) * r * 0.72f);
        const ImVec2 p1(c.x + std::cos(a) * r * 1.00f, c.y + std::sin(a) * r * 1.00f);
        dl->AddLine(p0, p1, col, 1.6f);
    }
}

/// Draws a crescent moon centred in `c`, radius `r`, as a disc minus an offset disc.
void glyph_moon(ImDrawList* dl, ImVec2 c, float r, ImU32 col, ImU32 knockout) {
    dl->AddCircleFilled(ImVec2(c.x + r * 0.10f, c.y), r * 0.78f, col, 24);
    dl->AddCircleFilled(ImVec2(c.x + r * 0.48f, c.y - r * 0.26f), r * 0.66f, knockout, 24);
}

/// Draws a gear glyph: a ring with square teeth.
void glyph_gear(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddCircle(c, r * 0.52f, col, 0, 1.7f);
    for (int i = 0; i < 6; ++i) {
        const float a = static_cast<float>(i) * 1.0471975f;    // 60 degrees
        const ImVec2 p0(c.x + std::cos(a) * r * 0.62f, c.y + std::sin(a) * r * 0.62f);
        const ImVec2 p1(c.x + std::cos(a) * r * 0.98f, c.y + std::sin(a) * r * 0.98f);
        dl->AddLine(p0, p1, col, 2.4f);
    }
}

/// Draws a person glyph: head over shoulders.
void glyph_account(ImDrawList* dl, ImVec2 c, float r, ImU32 col) {
    dl->AddCircle(ImVec2(c.x, c.y - r * 0.34f), r * 0.36f, col, 0, 1.7f);
    dl->PathArcTo(ImVec2(c.x, c.y + r * 0.86f), r * 0.72f, 3.3161f, 6.1087f, 20);  // shoulders
    dl->PathStroke(col, 0, 1.7f);
}

/// A pill-shaped sun/moon theme switch: two halves, with the active one highlighted.
///
/// Returns true when clicked. Reads as a switch rather than a label, which a text button showing
/// either the current or the target theme never quite does.
bool theme_switch(bool dark, float height) {
    const float w = height * 2.05f;
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##theme_switch", ImVec2(w, height));
    const bool clicked = ImGui::IsItemClicked();
    const bool hover   = ImGui::IsItemHovered();

    ImDrawList*       dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& st = ImGui::GetStyle();

    const ImU32 track  = ImGui::GetColorU32(st.Colors[dark ? ImGuiCol_FrameBg : ImGuiCol_Header]);
    const ImU32 knob   = ImGui::GetColorU32(st.Colors[hover ? ImGuiCol_HeaderHovered : ImGuiCol_HeaderActive]);
    const ImU32 on     = ImGui::GetColorU32(st.Colors[ImGuiCol_Text]);
    const ImU32 off    = ImGui::GetColorU32(st.Colors[ImGuiCol_TextDisabled]);

    const float rad = height * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height), track, rad);

    // The knob sits over the ACTIVE half: sun on the left in light, moon on the right in dark.
    const float half = w * 0.5f;
    const ImVec2 kp(p.x + (dark ? half : 0.0f), p.y);
    dl->AddRectFilled(kp, ImVec2(kp.x + half, kp.y + height), knob, rad);

    const float gr = height * 0.30f;
    glyph_sun(dl, ImVec2(p.x + half * 0.5f, p.y + rad), gr, dark ? off : on);
    glyph_moon(dl, ImVec2(p.x + half * 1.5f, p.y + rad), gr, dark ? on : off,
               dark ? knob : track);

    return clicked;
}

/// An icon-only menu-bar button. Returns true when clicked.
bool icon_button(const char* id, float size, void (*draw)(ImDrawList*, ImVec2, float, ImU32),
                 bool enabled) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool clicked = enabled && ImGui::IsItemClicked();
    const bool hover   = ImGui::IsItemHovered();

    const ImGuiStyle& st = ImGui::GetStyle();
    ImU32 col = ImGui::GetColorU32(st.Colors[enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
    if (enabled && hover) col = ImGui::GetColorU32(st.Colors[ImGuiCol_CheckMark]);

    draw(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.42f,
         col);
    return clicked;
}

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

    // Right-hand cluster: account, theme switch, settings - all icons. Laid out right-to-left
    // from the window edge so adding or removing one does not shift the others.
    {
        const ImGuiStyle& st   = ImGui::GetStyle();
        const bool        dark = (current_theme() == Theme::Dark);

        const float icon  = ImGui::GetFrameHeight() * 0.86f;
        const float sw    = icon * 2.05f;                       // theme switch is a wide pill
        const float gap   = st.ItemSpacing.x;
        const float total = icon + gap + sw + gap + icon + st.WindowPadding.x;

        ImGui::SetCursorPosX(ImGui::GetWindowWidth() - total);
        const float y = (ImGui::GetWindowHeight() - icon) * 0.5f;

        ImGui::SetCursorPosY(y);
        icon_button("##account", icon, glyph_account, /*enabled=*/false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Account - sign-in, for the scene assistant's API key.\n\n"
                              "Not yet implemented.");

        ImGui::SameLine(0.0f, gap);
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - icon) * 0.5f);
        if (theme_switch(dark, icon)) {
            const Theme next = dark ? Theme::Light : Theme::Dark;
            apply_theme(next);
            polyscope::view::bgColor = viewport_background(next);  // panels and 3D together
            polyscope::requestRedraw();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Switch to the %s theme.", dark ? "light" : "dark");

        ImGui::SameLine(0.0f, gap);
        ImGui::SetCursorPosY((ImGui::GetWindowHeight() - icon) * 0.5f);
        if (icon_button("##settings", icon, glyph_gear, /*enabled=*/true) && ctx.open_settings)
            ctx.open_settings();
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

    // Import / export on top. One button, since they are the same dialog in two directions.
    ImGui::Button("<>", ImVec2(kBtn, kBtn));
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Import a 3D model, or export the scene and its results.\n\n"
                          "Not yet implemented - use the Import panel on the left for now.");

    // Assistant sits lowest, nearest the thumb, and is accent-filled as the primary of the pair.
    ImGui::PushStyleColor(ImGuiCol_Button,        st.Colors[ImGuiCol_CheckMark]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, st.Colors[ImGuiCol_SeparatorHovered]);
    ImGui::PushStyleColor(ImGuiCol_Text,          ImVec4(0.10f, 0.07f, 0.02f, 1.0f));
    ImGui::Button("AI", ImVec2(kBtn, kBtn));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Scene assistant - describe a cooker, attach reference photos and\n"
                          "specification documents, and have a scene generated.\n\n"
                          "Not yet implemented.");

    st.FrameRounding = saved;
    ImGui::End();
    (void)ctx;
}

} // namespace scrt::viz
