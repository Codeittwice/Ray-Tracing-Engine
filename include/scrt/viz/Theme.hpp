#pragma once
#include "imgui.h"

#include <array>
#include <string>

namespace scrt::viz {

/// Colour scheme for the interface. Dark is the default; light exists because screenshots for
/// reports and printed documentation read better on a light ground.
enum class Theme { Dark = 0, Light = 1 };

/// Applies `t` to the current ImGui context and remembers it as the active theme.
///
/// Safe to call at any time: nothing in Polyscope re-applies its own style per frame, so a style
/// written here persists for the life of the context. It must, however, be re-applied whenever a
/// new ImGui context appears - polyscope::show() creates one - which is why the Viewer routes it
/// through options::configureImGuiStyleCallback rather than calling it once after init().
void apply_theme(Theme t);

/// The theme last passed to apply_theme().
Theme current_theme();

/// Viewport clear colour for `t`, as Polyscope's RGBA float array.
///
/// The 3D area is most of the screen, so leaving it at Polyscope's near-white while the panels
/// are dark reads as two applications sharing a window.
std::array<float, 4> viewport_background(Theme t);

/// Human-readable name, for the Settings panel.
const char* theme_name(Theme t);

/// Fill colour for a control that is ON or primary - the active transform tool, the assistant
/// button. Not the same as ImGuiCol_CheckMark: a tick mark and a solid 44px disc want different
/// amounts of the same hue, and the light theme's mark colour is dark enough to read as mud when
/// it covers a whole button.
ImVec4 accent_fill();

/// The readable foreground for text or an icon drawn on `bg`: near-black or near-white,
/// whichever has the higher WCAG contrast ratio against it.
///
/// Computed rather than hard-coded. The previous near-black was chosen against the dark theme's
/// light amber and carried over unchanged to the light theme's dark amber, where it left the
/// active tool button and the assistant icon hard to read.
ImVec4 readable_on(const ImVec4& bg);

} // namespace scrt::viz
