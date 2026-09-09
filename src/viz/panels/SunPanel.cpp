#include "scrt/viz/Panels.hpp"

#include "scrt/sources/SunSource.hpp"

#include "imgui.h"

namespace scrt::viz {

/// Draws the sun controls panel (DNI, azimuth, elevation).
void draw_sun_panel(PanelContext& ctx) {
    if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ctx.scene && ctx.scene->sun()) {
            auto* sun = ctx.scene->sun();

            float dni = static_cast<float>(sun->dni());
            if (ImGui::SliderFloat("DNI (W/m²)", &dni, 500.0f, 1500.0f)) {
                sun->set_dni(dni);
                if (ctx.need_retrace) *ctx.need_retrace = true;
            }

            sources::SunAngles angles = sun->sun_angles();
            float azimuth   = static_cast<float>(angles.azimuth_deg);
            float elevation = static_cast<float>(angles.elevation_deg);

            bool angle_changed = false;
            angle_changed |= ImGui::SliderFloat("Azimuth (deg)", &azimuth, 0.0f, 360.0f);
            // NOTE: a below-horizon sun (elevation < 0) is silently accepted by
            // SunSource::set_sun_angles / direction_from_angles — it yields 0 W
            // with no diagnostic (see Audit A1, open finding). This slider's
            // 0-90 range is a UI-only clamp; it does not reject or warn on the
            // underlying below-horizon case, which remains reachable via any
            // other caller of set_sun_angles (e.g. scene JSON).
            angle_changed |= ImGui::SliderFloat("Elevation (deg)", &elevation, 0.0f, 90.0f);

            if (angle_changed) {
                sun->set_sun_angles({static_cast<double>(azimuth),
                                     static_cast<double>(elevation)});
                if (ctx.need_retrace) *ctx.need_retrace = true;
            }
        }
    }
}

} // namespace scrt::viz
