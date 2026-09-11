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
    ImVec2 left_pos, left_size;    ///< Workspace column, docked to the left edge.
    ImVec2 right_pos, right_size;  ///< Analysis column, docked to the right edge.

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
/// Columns are clamped so that a narrow window keeps a usable 3D area rather than being entirely
/// consumed by panels.
inline LayoutRects compute_layout(ImVec2 display, float top_offset = 0.0f) {
    constexpr float kMargin      = 0.0f;  // panels sit flush against the window edges
    constexpr float kLeftWidth   = 340.0f;
    constexpr float kRightWidth  = 430.0f;
    constexpr float kMinViewport = 320.0f;

    // Give the 3D area priority when the window is too narrow for both columns at full width.
    float left  = kLeftWidth;
    float right = kRightWidth;
    const float wanted = left + right + kMinViewport + 3.0f * kMargin;
    if (display.x < wanted) {
        const float avail = std::max(0.0f, display.x - kMinViewport - 3.0f * kMargin);
        const float share = (kLeftWidth + kRightWidth) > 0.0f
                                ? avail / (kLeftWidth + kRightWidth)
                                : 0.0f;
        left  = std::max(180.0f, kLeftWidth * share);
        right = std::max(200.0f, kRightWidth * share);
    }

    const float top    = top_offset;
    const float height = std::max(120.0f, display.y - top);

    LayoutRects r;
    r.left_pos   = ImVec2(kMargin, top);
    r.left_size  = ImVec2(left, height);
    r.right_pos  = ImVec2(std::max(kMargin, display.x - right - kMargin), top);
    r.right_size = ImVec2(right, height);

    r.viewport_min = ImVec2(left, top);
    r.viewport_max = ImVec2(std::max(r.viewport_min.x, r.right_pos.x), top + height);
    return r;
}

/// Window flags for a docked panel: fixed by the layout, and never restored from imgui.ini.
inline ImGuiWindowFlags docked_panel_flags() {
    return ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
           ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;
}

} // namespace scrt::viz
