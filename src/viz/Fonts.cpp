#include "scrt/viz/Fonts.hpp"
#include "scrt/viz/Icons.hpp"

#include "imgui.h"
#include <GLFW/glfw3.h>

#include <algorithm>

namespace polyscope::render {
// Polyscope compiles Lato and Cousine in as compressed blobs and exposes them only through these
// four functions, declared in its own imgui_config.cpp rather than in a public header. They are
// forward-declared here for the same reason Polyscope forward-declares them there: we are
// replacing prepareImGuiFonts(), and the replacement has to add the two default faces itself, in
// order, because ImGui merges a font into whichever font precedes it in the config list.
unsigned int        getCousineRegularCompressedSize();
const unsigned int* getCousineRegularCompressedData();
unsigned int        getLatoRegularCompressedSize();
const unsigned int* getLatoRegularCompressedData();
} // namespace polyscope::render

namespace scrt::viz {

namespace {

ImFont* g_icons_large = nullptr;
float   g_scale       = 1.0f;

/// Codepoint range of the embedded subset, as ImGui wants it: a zero-terminated pair list which
/// must outlive the atlas build. Static, not a local - ImFontConfig stores the pointer.
const ImWchar kRanges[] = {kIconMin, kIconMax, 0};

constexpr float kRegularSize = 18.0f;  // Polyscope's own size; changing it re-flows every panel
constexpr float kMonoSize    = 16.0f;
constexpr float kIconSize    = 24.0f;  // for the circular viewport buttons

/// Reads the content scale of the monitor the window opened on.
///
/// Clamped: below 1 the interface would be smaller than designed for no good reason, and above 3
/// the font atlas grows past what a shared texture wants to be for no visible gain.
float detect_scale() {
    float sx = 1.0f, sy = 1.0f;
    if (GLFWwindow* win = glfwGetCurrentContext()) {
        glfwGetWindowContentScale(win, &sx, &sy);
    } else if (GLFWmonitor* mon = glfwGetPrimaryMonitor()) {
        glfwGetMonitorContentScale(mon, &sx, &sy);
    }
    const float s = std::max(sx, sy);
    return (s > 0.1f) ? std::clamp(s, 1.0f, 3.0f) : 1.0f;
}

} // namespace

std::tuple<ImFontAtlas*, ImFont*, ImFont*> prepare_fonts() {
    ImGuiIO& io = ImGui::GetIO();

    // Polyscope creates the GLFW window before it configures ImGui, so the monitor is known by
    // the time this runs and the faces can be rasterised at the size they will be drawn at.
    // Scaling a font up after rasterisation gives blurred text; this gives sharp text.
    g_scale = detect_scale();
    const float sz_regular = kRegularSize * g_scale;
    const float sz_mono    = kMonoSize * g_scale;
    const float sz_icon    = kIconSize * g_scale;

    ImFont* regular = io.Fonts->AddFontFromMemoryCompressedTTF(
        polyscope::render::getLatoRegularCompressedData(),
        static_cast<int>(polyscope::render::getLatoRegularCompressedSize()), sz_regular);

    {
        // Merged into `regular`, so icons resolve inside ordinary strings. Font Awesome's glyphs
        // are drawn on a square em while Lato's are not, so they land high on the text baseline
        // unless nudged; GlyphOffset.y is the correction, found by eye at 18px.
        ImFontConfig cfg;
        cfg.MergeMode            = true;
        cfg.PixelSnapH           = true;
        cfg.GlyphMinAdvanceX     = sz_regular;        // keeps icon-led labels aligned in a column
        cfg.GlyphOffset          = ImVec2(0.0f, 2.0f * g_scale);
        cfg.FontDataOwnedByAtlas = false;          // kIconFontTTF is static; ImGui must not free it
        io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kIconFontTTF),
                                       static_cast<int>(kIconFontTTFLen), sz_regular, &cfg,
                                       kRanges);
    }

    ImFont* mono = io.Fonts->AddFontFromMemoryCompressedTTF(
        polyscope::render::getCousineRegularCompressedData(),
        static_cast<int>(polyscope::render::getCousineRegularCompressedSize()), sz_mono);

    {
        // Standalone, not merged: the floating viewport buttons want the icon at button scale,
        // and pushing a whole larger text font for them would change their tooltips too.
        ImFontConfig cfg;
        cfg.PixelSnapH           = true;
        cfg.FontDataOwnedByAtlas = false;
        g_icons_large = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kIconFontTTF),
                                                       static_cast<int>(kIconFontTTFLen), sz_icon,
                                                       &cfg, kRanges);
    }

    io.Fonts->Build();
    return {io.Fonts, regular, mono};
}

ImFont* font_icons_large() { return g_icons_large; }

float ui_scale() { return g_scale; }

} // namespace scrt::viz
