#pragma once
#include "imgui.h"

#include <tuple>

namespace scrt::viz {

/// Builds the font atlas: Polyscope's Lato with Font Awesome merged in, plus Cousine mono and a
/// larger icon-only face for the floating viewport buttons.
///
/// Assigned to `polyscope::options::prepareImGuiFontsCallback` BEFORE `polyscope::init()`.
/// Returns `{atlas, regular, mono}` in the order Polyscope's render engine expects; it keeps the
/// atlas and shares it across every ImGui context it creates, so the ImFont pointers cached here
/// stay valid for the life of the process.
///
/// Merging matters: the icon glyphs live inside the regular font, so an ordinary label can carry
/// one - `ICON_FA_PLAY "  Trace"` is a single string, not two draw calls and a manual alignment.
std::tuple<ImFontAtlas*, ImFont*, ImFont*> prepare_fonts();

/// The icon face at button size, for icon-only controls that need to read at a glance.
/// Null until prepare_fonts() has run; callers must check, since Polyscope owns when that happens.
ImFont* font_icons_large();

/// Interface scale factor for this display, 1.0 on a 96 DPI screen and 2.5 on a 4K laptop panel
/// running at 250%.
///
/// ImGui has no notion of DPI: a font is however many pixels it was rasterised at, and a panel is
/// however many pixels the layout says. On a 3840-wide framebuffer the 360px panel this interface
/// asks for is a 9% sliver of unreadable 18px text. Every hard-coded pixel size - font sizes,
/// style metrics, the layout columns, the floating buttons - is multiplied by this.
///
/// Fixed at startup from the monitor the window opens on, because the font atlas is rasterised
/// once and Polyscope shares it across every ImGui context it creates. Dragging the window to a
/// differently-scaled monitor mid-session will not re-scale it.
float ui_scale();

} // namespace scrt::viz
