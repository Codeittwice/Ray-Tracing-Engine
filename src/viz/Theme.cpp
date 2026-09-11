#include "scrt/viz/Theme.hpp"

#include "imgui.h"

namespace scrt::viz {

namespace {

Theme g_theme = Theme::Dark;

/// Shorthand for an sRGB colour given as 0-255 bytes, which is how the palette is specified.
constexpr ImVec4 rgba(int r, int g, int b, float a = 1.0f) {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, a);
}

/// One palette, named by role rather than by colour, so the two themes stay in step.
struct Palette {
    ImVec4 window, child, popup, border;
    ImVec4 frame, frame_hover, frame_active;
    ImVec4 title, title_active;
    ImVec4 text, text_dim;
    ImVec4 accent, accent_hover, accent_active;
    ImVec4 header, header_hover, header_active;
    ImVec4 separator, scrollbar, scrollbar_grab;
};

/// Cool instrument greys with a slight cyan bias, and a solar amber accent - warm against cool,
/// and literal to the subject.
const Palette kDark{
    /*window*/       rgba(20, 28, 36, 0.97f),
    /*child*/        rgba(16, 23, 30, 0.60f),
    /*popup*/        rgba(20, 28, 36, 0.98f),
    /*border*/       rgba(42, 53, 64),
    /*frame*/        rgba(27, 36, 45),
    /*frame_hover*/  rgba(36, 47, 58),
    /*frame_active*/ rgba(44, 57, 70),
    /*title*/        rgba(16, 23, 30),
    /*title_active*/ rgba(27, 36, 45),
    /*text*/         rgba(216, 226, 234),
    /*text_dim*/     rgba(122, 140, 156),
    /*accent*/       rgba(232, 163, 61),
    /*accent_hover*/ rgba(244, 180, 86),
    /*accent_active*/rgba(214, 145, 44),
    /*header*/       rgba(33, 44, 55),
    /*header_hover*/ rgba(44, 58, 71),
    /*header_active*/rgba(54, 70, 86),
    /*separator*/    rgba(42, 53, 64),
    /*scrollbar*/    rgba(16, 23, 30),
    /*scrollbar_grab*/ rgba(54, 70, 86),
};

/// The same roles on a light ground. Deliberately not an inversion: the accent is darkened so it
/// still carries contrast against white, and the greys keep the same slight cyan bias.
const Palette kLight{
    /*window*/       rgba(246, 248, 250, 0.98f),
    /*child*/        rgba(236, 240, 244, 0.70f),
    /*popup*/        rgba(252, 253, 254, 0.99f),
    /*border*/       rgba(200, 211, 221),
    /*frame*/        rgba(233, 238, 242),
    /*frame_hover*/  rgba(221, 229, 236),
    /*frame_active*/ rgba(209, 220, 230),
    /*title*/        rgba(228, 234, 240),
    /*title_active*/ rgba(214, 224, 233),
    /*text*/         rgba(22, 34, 46),
    /*text_dim*/     rgba(102, 119, 136),
    /*accent*/       rgba(176, 110, 16),
    /*accent_hover*/ rgba(196, 126, 26),
    /*accent_active*/rgba(150, 92, 10),
    /*header*/       rgba(224, 231, 238),
    /*header_hover*/ rgba(211, 221, 230),
    /*header_active*/rgba(198, 211, 223),
    /*separator*/    rgba(200, 211, 221),
    /*scrollbar*/    rgba(236, 240, 244),
    /*scrollbar_grab*/rgba(186, 199, 211),
};

void apply_palette(const Palette& p) {
    ImGuiStyle& s = ImGui::GetStyle();
    ImVec4*     c = s.Colors;

    c[ImGuiCol_WindowBg]        = p.window;
    c[ImGuiCol_ChildBg]         = p.child;
    c[ImGuiCol_PopupBg]         = p.popup;
    c[ImGuiCol_Border]          = p.border;
    c[ImGuiCol_BorderShadow]    = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]         = p.frame;
    c[ImGuiCol_FrameBgHovered]  = p.frame_hover;
    c[ImGuiCol_FrameBgActive]   = p.frame_active;

    c[ImGuiCol_TitleBg]         = p.title;
    c[ImGuiCol_TitleBgActive]   = p.title_active;
    c[ImGuiCol_TitleBgCollapsed]= p.title;
    c[ImGuiCol_MenuBarBg]       = p.title;

    c[ImGuiCol_Text]            = p.text;
    c[ImGuiCol_TextDisabled]    = p.text_dim;

    c[ImGuiCol_CheckMark]       = p.accent;
    c[ImGuiCol_SliderGrab]      = p.accent;
    c[ImGuiCol_SliderGrabActive]= p.accent_active;
    c[ImGuiCol_Button]          = p.header;
    c[ImGuiCol_ButtonHovered]   = p.header_hover;
    c[ImGuiCol_ButtonActive]    = p.header_active;

    c[ImGuiCol_Header]          = p.header;
    c[ImGuiCol_HeaderHovered]   = p.header_hover;
    c[ImGuiCol_HeaderActive]    = p.header_active;

    c[ImGuiCol_Separator]       = p.separator;
    c[ImGuiCol_SeparatorHovered]= p.accent_hover;
    c[ImGuiCol_SeparatorActive] = p.accent;

    c[ImGuiCol_ScrollbarBg]     = p.scrollbar;
    c[ImGuiCol_ScrollbarGrab]   = p.scrollbar_grab;
    c[ImGuiCol_ScrollbarGrabHovered] = p.header_hover;
    c[ImGuiCol_ScrollbarGrabActive]  = p.accent;

    c[ImGuiCol_Tab]             = p.frame;
    c[ImGuiCol_TabHovered]      = p.header_hover;
    c[ImGuiCol_TabActive]       = p.header_active;
    c[ImGuiCol_TabUnfocused]    = p.frame;
    c[ImGuiCol_TabUnfocusedActive] = p.header;

    c[ImGuiCol_ResizeGrip]      = ImVec4(0, 0, 0, 0);   // panels are docked; no grip to show
    c[ImGuiCol_ResizeGripHovered] = p.accent_hover;
    c[ImGuiCol_ResizeGripActive]  = p.accent;

    c[ImGuiCol_PlotLines]       = p.accent;
    c[ImGuiCol_PlotLinesHovered]= p.accent_hover;
    c[ImGuiCol_PlotHistogram]   = p.accent;
    c[ImGuiCol_PlotHistogramHovered] = p.accent_hover;

    c[ImGuiCol_TextSelectedBg]  = ImVec4(p.accent.x, p.accent.y, p.accent.z, 0.35f);
    c[ImGuiCol_NavHighlight]    = p.accent;

    // Squarer and tighter than Polyscope's defaults: this is an instrument, not a consumer app.
    s.WindowRounding    = 2.0f;
    s.ChildRounding     = 2.0f;
    s.FrameRounding     = 2.0f;
    s.PopupRounding     = 2.0f;
    s.ScrollbarRounding = 2.0f;
    s.GrabRounding      = 2.0f;
    s.TabRounding       = 2.0f;
    s.WindowBorderSize  = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.WindowPadding     = ImVec2(9, 9);
    s.FramePadding      = ImVec2(7, 4);
    s.ItemSpacing       = ImVec2(7, 6);
    s.ItemInnerSpacing  = ImVec2(6, 5);
    s.ScrollbarSize     = 13.0f;
    s.GrabMinSize       = 9.0f;
}

} // namespace

void apply_theme(Theme t) {
    g_theme = t;
    apply_palette(t == Theme::Light ? kLight : kDark);
}

Theme current_theme() { return g_theme; }

std::array<float, 4> viewport_background(Theme t) {
    // A shade darker than the panels in dark, a shade lighter in light, so the panels still read
    // as objects sitting on the view rather than dissolving into it.
    return t == Theme::Light ? std::array<float, 4>{0.882f, 0.902f, 0.922f, 1.0f}
                             : std::array<float, 4>{0.024f, 0.039f, 0.051f, 1.0f};
}

const char* theme_name(Theme t) { return t == Theme::Light ? "Light" : "Dark"; }

} // namespace scrt::viz
