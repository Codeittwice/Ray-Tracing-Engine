#pragma once
#include "imgui.h"
#include <algorithm>

namespace scrt::viz {

/// Screen rectangles for the docked panel layout, recomputed from the window size every frame.
///
/// Why every frame: ImGui's `ImGuiCond_FirstUseEver` positions a window once and never again, so
/// the panels froze wherever they were last left and ignored a window resize. Polyscope's own
/// windows reposition unconditionally every frame, which is why theirs re-flowed and ours did not.
/// These rects are therefore applied with `ImGuiCond_Always`.
struct LayoutRects {
    ImVec2 left_pos, left_size;                  ///< Workspace column, docked to the left edge.
    ImVec2 right_top_pos, right_top_size;        ///< Flux analysis.
    ImVec2 right_bottom_pos, right_bottom_size;  ///< Selection, and the transform under it.

    /// The 3D area left over between the two columns.
    ///
    /// Edge-panning during a gizmo drag must trigger on THIS rectangle, not on the window: once
    /// the columns occupy the screen edges, the window edge sits behind a panel and dragging an
    /// object toward it would never pan. Carrying an object past the visible area without
    /// releasing it is deliberate behaviour, so it has to follow the viewport instead.
    ImVec2 viewport_min, viewport_max;
};

/// Computes the docked layout for a window of `display` size.
///
/// `top_offset` is the menu bar's height. `bottom_height` is how much of the right column the
/// selection panel asks for, in pixels - it is content-driven, because that panel holds a short
/// paragraph when nothing is selected and a full transform editor when something is.
///
/// Columns are clamped so that a narrow window keeps a usable 3D area rather than being entirely
/// consumed by panels.
inline LayoutRects compute_layout(ImVec2 display, float top_offset = 0.0f,
                                  float bottom_height = 300.0f, float scale = 1.0f) {
    // Design sizes, in pixels at 96 DPI, multiplied up for the display's scale factor. Without
    // that the left column is a 360px sliver of a 3840px-wide 4K framebuffer.
    const float kMargin      = 0.0f;   // panels sit flush against the window edges
    // 390, not 360: the workspace column gained a fourth tab in Wave 3 (Library), and at 360
    // ImGui clipped every tab label to "Scen...", "Libra...". Thirty pixels is invisible to the
    // 3D area and buys four legible tabs.
    const float kLeftWidth   = 390.0f * scale;
    const float kRightWidth  = 440.0f * scale;
    const float kMinViewport = 320.0f * scale;

    // Give the 3D area priority when the window is too narrow for both columns at full width.
    float       left   = kLeftWidth;
    float       right  = kRightWidth;
    const float wanted = left + right + kMinViewport + 3.0f * kMargin;
    if (display.x < wanted) {
        const float avail = std::max(0.0f, display.x - kMinViewport - 3.0f * kMargin);
        const float share =
            (kLeftWidth + kRightWidth) > 0.0f ? avail / (kLeftWidth + kRightWidth) : 0.0f;
        left  = std::max(180.0f * scale, kLeftWidth * share);
        right = std::max(200.0f * scale, kRightWidth * share);
    }

    const float top    = top_offset;
    const float height = std::max(120.0f * scale, display.y - top);

    // The flux plot needs real vertical room to stay readable, so the selection panel gives way
    // first on a short window rather than squeezing the plot to a strip. Half the column is the
    // hard ceiling: with a surface selected the transform editor asks for more than that, and
    // the plot is the reason the right column exists.
    const float bottom_cap = std::max(120.0f * scale, height * 0.5f);
    const float bottom     = std::clamp(bottom_height * scale, 120.0f * scale, bottom_cap);

    LayoutRects r;
    r.left_pos   = ImVec2(kMargin, top);
    r.left_size  = ImVec2(left, height);

    const float right_x = std::max(kMargin, display.x - right - kMargin);
    r.right_top_pos     = ImVec2(right_x, top);
    r.right_top_size    = ImVec2(right, height - bottom);
    r.right_bottom_pos  = ImVec2(right_x, top + height - bottom);
    r.right_bottom_size = ImVec2(right, bottom);

    r.viewport_min = ImVec2(left, top);
    r.viewport_max = ImVec2(std::max(r.viewport_min.x, right_x), top + height);
    return r;
}

/// Window flags for a docked panel: fixed by the layout, and never restored from imgui.ini.
inline ImGuiWindowFlags docked_panel_flags() {
    return ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
}

} // namespace scrt::viz
