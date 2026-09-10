#include "scrt/viz/Panels.hpp"

#include "scrt/sources/SunSource.hpp"
#include "scrt/viz/FluxPlotter.hpp"

#include <cmath>

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Attaches a hover tooltip to the widget just drawn.
void tip(const char* text) {
    // BeginItemTooltip() already performs the IsItemHovered(ForTooltip) test and its
    // hover delay, so it must not be combined with a second hover check.
    if (ImGui::BeginItemTooltip()) {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 26.0f);
        ImGui::TextUnformatted(text);  // Unformatted: '%' in the text is NOT a format spec.
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

/// Grey explanatory line, wrapped to the panel width; for plain-language captions.
void help_line(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/// Nearest 16-point compass abbreviation for a bearing in degrees.
///
/// Display only: the azimuth itself stays the exact slider value in the readout beside it.
/// This just spares the reader from converting 247.5 into "roughly west-southwest".
const char* compass_point(float bearing_deg) {
    static const char* kPoints[16] = {
        "N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
        "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    // Wrap first: the slider is clamped to [0, 360], but 360 itself must land back on N.
    float b = std::fmod(bearing_deg, 360.0f);
    if (b < 0.0f) b += 360.0f;
    const int idx = static_cast<int>((b / 22.5f) + 0.5f) & 15;  // & 15 folds 16 -> 0.
    return kPoints[idx];
}

} // namespace

/// Draws the sun controls panel (DNI, azimuth, elevation).
void draw_sun_panel(PanelContext& ctx) {
    if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ctx.scene && ctx.scene->sun()) {
            auto* sun = ctx.scene->sun();

            help_line("Where the sun sits in the sky and how strong it is. This is a "
                      "single instant, not a whole day.");
            ImGui::Separator();

            // The Flux Analysis window and its JSON export read the DNI from here.
            // Without this the plotter falls back to a 1000 W/m2 placeholder and an
            // exported concentration ratio ignores the slider entirely.
            FluxPlotter::set_scene_dni(sun->dni());

            float dni = static_cast<float>(sun->dni());
            if (ImGui::SliderFloat("DNI (W/m²)", &dni, 500.0f, 1500.0f)) {
                sun->set_dni(dni);
                if (ctx.need_retrace) *ctx.need_retrace = true;
            }
            tip("Direct Normal Irradiance: how much sunlight arrives per square metre "
                "of surface held square-on to the beam, in watts per square metre.\n\n"
                "This is the direct beam only — the part that casts a sharp shadow and "
                "the only part a mirror can focus. Around 1000 W/m² is a clear day at "
                "sea level with the sun high; haze, cloud or a low sun cut it down. "
                "Every result in this app scales directly with this number.");

            sources::SunAngles angles = sun->sun_angles();
            float azimuth   = static_cast<float>(angles.azimuth_deg);
            float elevation = static_cast<float>(angles.elevation_deg);

            bool angle_changed = false;

            angle_changed |= ImGui::SliderFloat("Azimuth (compass bearing, deg)",
                                                &azimuth, 0.0f, 360.0f);
            tip("The compass direction the sun lies in, as a bearing in degrees.\n\n"
                "0 is due north, 90 is east, 180 is south, 270 is west — the same way a "
                "compass reads. It says which way round the cooker the sun sits, not how "
                "high it is.\n\n"
                "In the northern hemisphere the midday sun is near 180 (south).");

            // NOTE: a below-horizon sun (elevation < 0) is still reachable from scene
            // JSON and from any other caller of set_sun_angles — this slider's 0-90 range
            // is a UI-only clamp, not a validation. SunSource::below_horizon() is the
            // predicate that decides whether such a sun should be simulated, and
            // scene::Aperture::auto_fit() throws on one. The status line below reports it
            // if a loaded scene arrives with the sun already under the horizon.
            angle_changed |= ImGui::SliderFloat("Elevation (deg above horizon)",
                                                &elevation, 0.0f, 90.0f);
            tip("How high the sun is above the horizon, in degrees.\n\n"
                "0 means the sun is sitting on the horizon at sunrise or sunset, and 90 "
                "means it is directly overhead. It is the angle you would measure by "
                "looking up from flat ground.\n\n"
                "A low sun spreads the same beam across more ground, so a flat collector "
                "gathers less: the power falls off as sin(elevation). At 30 degrees a "
                "horizontal collector sees half what it would at 90.");

            if (angle_changed) {
                sun->set_sun_angles({static_cast<double>(azimuth),
                                     static_cast<double>(elevation)});
                if (ctx.need_retrace) *ctx.need_retrace = true;
            }

            // ---- Plain-language restatement of the two sliders ------------------
            // Full precision is preserved: the exact slider values appear here to two
            // decimals, and the compass word is added beside them, never instead of them.
            ImGui::Separator();
            if (sun->sun_below_horizon()) {
                ImGui::TextColored({1.0f, 0.5f, 0.2f, 1.0f},
                                   "Sun is below the horizon (%.2f deg) — no daylight.",
                                   static_cast<double>(elevation));
                help_line("This scene's sun has set, so a trace collects no power. Raise "
                          "the elevation above 0.");
            } else {
                ImGui::Text("Sun %.2f deg above the horizon, bearing %.2f deg (%s).",
                            static_cast<double>(elevation),
                            static_cast<double>(azimuth), compass_point(azimuth));
                help_line("Elevation 90 is straight overhead; bearing 0 is north and 180 "
                          "is south.");
            }
        }
    }
}

} // namespace scrt::viz
