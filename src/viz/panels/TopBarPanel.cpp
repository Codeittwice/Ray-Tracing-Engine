#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Fonts.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/Theme.hpp"

#include "polyscope/polyscope.h"
#include "polyscope/view.h"

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Draws one icon glyph centred on `c`.
///
/// The icons are Font Awesome glyphs merged into the regular face, so they are text - but text
/// laid out by the cursor, which is no use for a control that has to place a glyph inside a
/// shape it also draws. This puts one where it is told.
void icon_at(ImDrawList* dl, ImVec2 c, float size, ImU32 col, const char* icon) {
    ImFont* f  = ImGui::GetFont();
    ImVec2  sz = f->CalcTextSizeA(size, FLT_MAX, 0.0f, icon);
    dl->AddText(f, size, ImVec2(c.x - sz.x * 0.5f, c.y - sz.y * 0.5f), col, icon);
}

/// A pill-shaped sun/moon theme switch: two halves, with the knob over the active one.
///
/// Returns true when clicked. Reads as a switch rather than a label, which a text button showing
/// either the current or the target theme never quite does.
bool theme_switch(bool dark, float height) {
    const float  w = height * 2.05f;
    const ImVec2 p = ImGui::GetCursorScreenPos();

    ImGui::InvisibleButton("##theme_switch", ImVec2(w, height));
    const bool clicked = ImGui::IsItemClicked();
    const bool hover   = ImGui::IsItemHovered();

    ImDrawList*       dl = ImGui::GetWindowDrawList();
    const ImGuiStyle& st = ImGui::GetStyle();

    const ImU32 track = ImGui::GetColorU32(st.Colors[dark ? ImGuiCol_FrameBg : ImGuiCol_Header]);
    const ImU32 knob =
        ImGui::GetColorU32(st.Colors[hover ? ImGuiCol_HeaderHovered : ImGuiCol_HeaderActive]);
    const ImU32 on  = ImGui::GetColorU32(st.Colors[ImGuiCol_Text]);
    const ImU32 off = ImGui::GetColorU32(st.Colors[ImGuiCol_TextDisabled]);

    const float rad = height * 0.5f;
    dl->AddRectFilled(p, ImVec2(p.x + w, p.y + height), track, rad);

    // The knob sits over the ACTIVE half: sun on the left in light, moon on the right in dark.
    const float  half = w * 0.5f;
    const ImVec2 kp(p.x + (dark ? half : 0.0f), p.y);
    dl->AddRectFilled(kp, ImVec2(kp.x + half, kp.y + height), knob, rad);

    const float gs = height * 0.62f;
    icon_at(dl, ImVec2(p.x + half * 0.5f, p.y + rad), gs, dark ? off : on, ICON_FA_SUN);
    icon_at(dl, ImVec2(p.x + half * 1.5f, p.y + rad), gs, dark ? on : off, ICON_FA_MOON);

    return clicked;
}

/// An icon-only menu-bar button. Returns true when clicked.
bool icon_button(const char* id, float size, const char* icon, bool enabled) {
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton(id, ImVec2(size, size));
    const bool clicked = enabled && ImGui::IsItemClicked();
    const bool hover   = ImGui::IsItemHovered();

    const ImGuiStyle& st = ImGui::GetStyle();
    ImU32 col = ImGui::GetColorU32(st.Colors[enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled]);
    if (enabled && hover) col = ImGui::GetColorU32(st.Colors[ImGuiCol_CheckMark]);

    icon_at(ImGui::GetWindowDrawList(), ImVec2(p.x + size * 0.5f, p.y + size * 0.5f), size * 0.68f,
            col, icon);
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
        placeholder(ICON_FA_FILE "  New scene", "Ctrl+N");
        placeholder(ICON_FA_FOLDER_OPEN "  Open...", "Ctrl+O");
        placeholder("Open recent");
        ImGui::Separator();
        placeholder(ICON_FA_FLOPPY_DISK "  Save", "Ctrl+S");
        placeholder("Save as...", "Ctrl+Shift+S");
        placeholder(ICON_FA_ARROW_ROTATE_LEFT "  Revert");
        ImGui::Separator();
        placeholder(ICON_FA_XMARK "  Exit");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Project")) {
        placeholder(ICON_FA_SLIDERS "  Scene properties");
        placeholder(ICON_FA_PALETTE "  Materials library");
        placeholder(ICON_FA_CHART_SIMPLE "  Batch compare...");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Import / Export")) {
        placeholder(ICON_FA_CUBE "  Import 3D model...");
        placeholder(ICON_FA_FILE_IMPORT "  Import scene JSON...");
        ImGui::Separator();
        placeholder(ICON_FA_FILE_EXPORT "  Export flux map (CSV)");
        placeholder(ICON_FA_FILE_EXPORT "  Export summary (JSON)");
        placeholder(ICON_FA_FILE_EXPORT "  Export scene (OBJ)");
        placeholder(ICON_FA_FILE "  Save screenshot");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        placeholder(ICON_FA_CROSSHAIRS "  Frame scene");
        placeholder(ICON_FA_MAGNIFYING_GLASS "  Top / Front / Side");
        ImGui::Separator();
        placeholder(ICON_FA_DIAGRAM_PROJECT "  Show rays");
        placeholder(ICON_FA_SQUARE "  Show aperture");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        placeholder(ICON_FA_CIRCLE_INFO "  Quick start");
        placeholder(ICON_FA_FILE "  Scene file reference");
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
        icon_button("##account", icon, ICON_FA_USER, /*enabled=*/false);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Account - sign-in, for the scene assistant's API key.\n\n"
                              "Not yet implemented.");

        ImGui::SameLine(0.0f, gap);
        ImGui::SetCursorPosY(y);
        if (theme_switch(dark, icon)) {
            const Theme next = dark ? Theme::Light : Theme::Dark;
            apply_theme(next);
            polyscope::view::bgColor = viewport_background(next);  // panels and 3D together
            polyscope::requestRedraw();
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Switch to the %s theme.", dark ? "light" : "dark");

        ImGui::SameLine(0.0f, gap);
        ImGui::SetCursorPosY(y);
        if (icon_button("##settings", icon, ICON_FA_GEAR, /*enabled=*/true) && ctx.open_settings)
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

    const float     k      = ui_scale();
    const float     kBtn   = 44.0f * k;
    const float     kGap   = 9.0f * k;
    const float     kInset = 16.0f * k;
    constexpr int   kCount = 2;

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

    ImGuiStyle& st   = ImGui::GetStyle();
    const float save = st.FrameRounding;
    st.FrameRounding = kBtn * 0.5f;      // circular

    // The icon-only face, at button scale. Null only if Polyscope never ran our font callback,
    // in which case the labels fall back to the merged icons at text size rather than vanishing.
    ImFont* big = font_icons_large();
    if (big) ImGui::PushFont(big);

    // Import / export on top. One button, since they are the same dialog in two directions.
    ImGui::Button(ICON_FA_RIGHT_LEFT "##io", ImVec2(kBtn, kBtn));
    const bool io_hover = ImGui::IsItemHovered();

    // Assistant sits lowest, nearest the thumb, and is accent-filled as the primary of the pair.
    ImGui::PushStyleColor(ImGuiCol_Button, st.Colors[ImGuiCol_CheckMark]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, st.Colors[ImGuiCol_SeparatorHovered]);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.10f, 0.07f, 0.02f, 1.0f));
    ImGui::Button(ICON_FA_WAND_MAGIC_SPARKLES "##ai", ImVec2(kBtn, kBtn));
    ImGui::PopStyleColor(3);
    const bool ai_hover = ImGui::IsItemHovered();

    if (big) ImGui::PopFont();   // tooltips below belong at text size, not button size

    if (io_hover)
        ImGui::SetTooltip("Import a 3D model, or export the scene and its results.\n\n"
                          "Not yet implemented - use the Import panel on the left for now.");
    if (ai_hover)
        ImGui::SetTooltip("Scene assistant - describe a cooker, attach reference photos and\n"
                          "specification documents, and have a scene generated.\n\n"
                          "Not yet implemented.");

    st.FrameRounding = save;
    ImGui::End();
    (void)ctx;
}

} // namespace scrt::viz
