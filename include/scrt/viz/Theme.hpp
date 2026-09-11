#pragma once
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

/// Human-readable name, for the Settings panel.
const char* theme_name(Theme t);

} // namespace scrt::viz
