#include "scrt/viz/Panels.hpp"

#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/RealMirror.hpp"

#include "imgui.h"

namespace scrt::viz {

/// Draws the material parameter editor panel.
void draw_materials_panel(PanelContext& ctx) {
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ctx.scene) {
            for (auto& mat_ptr : ctx.scene->mutable_materials()) {
                ImGui::PushID(mat_ptr.get());
                ImGui::Text("%s", mat_ptr->name().c_str());
                bool changed = false;

                if (auto* rm = dynamic_cast<materials::RealMirror*>(mat_ptr.get())) {
                    float rho = static_cast<float>(rm->reflectance());
                    float se  = static_cast<float>(rm->slope_error());
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Reflectance##rm", &rho, 0.0f, 1.0f))
                        { rm->set_reflectance(rho); changed = true; }
                    if (ImGui::SliderFloat("Slope err (mrad)", &se, 0.0f, 10.0f))
                        { rm->set_slope_error_mrad(se); changed = true; }
                    ImGui::Unindent();
                } else if (auto* di = dynamic_cast<materials::Dielectric*>(mat_ptr.get())) {
                    float n     = static_cast<float>(di->n());
                    float alpha = static_cast<float>(di->absorption());
                    ImGui::Indent();
                    if (ImGui::SliderFloat("n##di", &n, 1.0f, 3.0f))
                        { di->set_n(n); changed = true; }
                    if (ImGui::SliderFloat("Absorb (1/m)", &alpha, 0.0f, 50.0f))
                        { di->set_absorption(alpha); changed = true; }
                    ImGui::Unindent();
                }

                if (changed && ctx.need_retrace) *ctx.need_retrace = true;
                ImGui::PopID();
                ImGui::Separator();
            }
        }
    }
}

} // namespace scrt::viz
