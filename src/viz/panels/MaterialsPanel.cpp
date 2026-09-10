#include "scrt/viz/Panels.hpp"

#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/RealMirror.hpp"

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Attaches a hover tooltip to the widget just drawn.
///
/// Tooltips rather than inline captions: the audience leads with cooker builders who need
/// "slope error" explained, but the expert path is unchanged and no vertical space is
/// spent on text an expert does not need. Every control below keeps its physical name and
/// its units in the visible label; the tooltip only adds meaning.
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

} // namespace

/// Draws the material parameter editor panel.
void draw_materials_panel(PanelContext& ctx) {
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ctx.scene) {
            help_line("What each surface is made of. These settings decide how much "
                      "sunlight survives each bounce and how tightly it stays focused.");
            ImGui::Separator();

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
                    tip("Fraction of light the mirror bounces back, from 0 (black) to 1 "
                        "(perfect).\n\n"
                        "Kitchen foil is roughly 0.85, polished aluminium 0.90, and a "
                        "good back-silvered glass mirror 0.95. The rest is absorbed and "
                        "lost as heat in the mirror itself. Every bounce multiplies: two "
                        "bounces at 0.90 deliver 0.81 of the sunlight, not 0.90.");

                    // The visible label keeps the physical name and the unit — a builder
                    // who reads the tooltip and then talks to a supplier needs the real
                    // term. "Slope error", not "roughness"; mrad, not "a bit wobbly".
                    if (ImGui::SliderFloat("Slope error (mrad)##rm", &se, 0.0f, 10.0f))
                        { rm->set_slope_error_mrad(se); changed = true; }
                    tip("How far the surface tilts away from a perfect mirror, in "
                        "milliradians (1 mrad = 0.057 degrees).\n\n"
                        "This is about the surface being gently wavy, not dirty. A "
                        "reflected ray leaves at roughly twice the local tilt, so the "
                        "error is doubled by the time light reaches the pot.\n\n"
                        "Higher numbers spread the focus into a wider, cooler blob: the "
                        "same total power lands, but over more area, so the hot spot "
                        "loses intensity. 0 is a flawless optical mirror. Around 2-5 mrad "
                        "is typical for a carefully built home reflector; a foil-covered "
                        "surface glued to plywood is worse.");

                    ImGui::Unindent();
                } else if (auto* di = dynamic_cast<materials::Dielectric*>(mat_ptr.get())) {
                    float n     = static_cast<float>(di->n());
                    float alpha = static_cast<float>(di->absorption());
                    ImGui::Indent();

                    if (ImGui::SliderFloat("Refractive index n##di", &n, 1.0f, 3.0f))
                        { di->set_n(n); changed = true; }
                    tip("How strongly this material bends light passing through it.\n\n"
                        "Air is 1.0, water 1.33, window glass about 1.5, acrylic 1.49. "
                        "A higher index bends light more sharply, so a lens of the same "
                        "shape focuses closer in. It also reflects slightly more light "
                        "straight off the front face, which is light the pot never sees.");

                    if (ImGui::SliderFloat("Absorption (1/m)##di", &alpha, 0.0f, 50.0f))
                        { di->set_absorption(alpha); changed = true; }
                    tip("How much light the material swallows per metre travelled, in "
                        "inverse metres.\n\n"
                        "0 is perfectly clear. The surviving fraction is exp(-absorption "
                        "x thickness), so 10 /m through 5 mm of glass passes about 95%. "
                        "This is why a thick lens can beat a thin one on focusing and "
                        "still deliver less power.");

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
