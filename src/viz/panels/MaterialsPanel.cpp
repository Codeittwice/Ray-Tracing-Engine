#include "scrt/viz/Panels.hpp"
#include "scrt/scene/SceneEditor.hpp"

#include "scrt/materials/BeamSplitter.hpp"
#include "scrt/materials/PolarisingOptics.hpp"
#include "scrt/materials/Diffuser.hpp"
#include "scrt/viz/Preview.hpp"
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

            // Every edit goes through the editor, which writes the live material AND its
            // MaterialDoc. Writing the live object alone - which this panel used to do - meant
            // dragging a slider and saving wrote the ORIGINAL value back to disk.
            auto set_param = [&ctx](const std::string& id, const char* key, double v) {
                if (ctx.editor) ctx.editor->commit_material_param(id, key, v);
                if (ctx.need_retrace) *ctx.need_retrace = true;
            };

            for (auto& mat_ptr : ctx.scene->mutable_materials()) {
                ImGui::PushID(mat_ptr.get());
                const std::string mat_id = mat_ptr->name();
                // The LIVE material, so dragging a slider below redraws this on the same frame:
                // you watch the physics change as you change it.
                draw_material_diagram(*mat_ptr, ImGui::GetFrameHeight() * 2.2f,
                                      ImGui::GetFrameHeight() * 1.7f);
                ImGui::SameLine();
                ImGui::Text("%s", mat_id.c_str());

                if (auto* rm = dynamic_cast<materials::RealMirror*>(mat_ptr.get())) {
                    float rho = static_cast<float>(rm->reflectance());
                    float se  = static_cast<float>(rm->slope_error());
                    ImGui::Indent();

                    if (ImGui::SliderFloat("Reflectance##rm", &rho, 0.0f, 1.0f))
                        set_param(mat_id, "reflectance", rho);
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
                        set_param(mat_id, "slope_error_mrad", se);
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
                        set_param(mat_id, "n", n);
                    tip("How strongly this material bends light passing through it.\n\n"
                        "Air is 1.0, water 1.33, window glass about 1.5, acrylic 1.49. "
                        "A higher index bends light more sharply, so a lens of the same "
                        "shape focuses closer in. It also reflects slightly more light "
                        "straight off the front face, which is light the pot never sees.");

                    if (ImGui::SliderFloat("Absorption (1/m)##di", &alpha, 0.0f, 50.0f))
                        set_param(mat_id, "absorption_per_m", alpha);
                    tip("How much light the material swallows per metre travelled, in "
                        "inverse metres.\n\n"
                        "0 is perfectly clear. The surviving fraction is exp(-absorption "
                        "x thickness), so 10 /m through 5 mm of glass passes about 95%. "
                        "This is why a thick lens can beat a thin one on focusing and "
                        "still deliver less power.");

                    ImGui::Unindent();
                } else if (auto* df = dynamic_cast<materials::Diffuser*>(mat_ptr.get())) {
                    float alb = static_cast<float>(df->albedo());
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Albedo##df", &alb, 0.0f, 1.0f))
                        set_param(mat_id, "albedo", alb);
                    tip("The share of light a matte surface sends back, scattered evenly in "
                        "every direction rather than bounced like a mirror.\n\n"
                        "A white calibration standard is 0.99, matte white paint 0.85, white "
                        "card 0.80. Matte black stove paint is about 0.04, which is how a "
                        "realistic black surface is modelled here: it keeps 96% and returns a "
                        "few percent into the scene, where the ideal absorber returns none.\n\n"
                        "This scatters over the whole hemisphere (Lambert's cosine law). A "
                        "ground-glass diffuser, which scatters in transmission, and a brushed "
                        "metal lobe are different models and are not implemented yet.");
                    ImGui::Unindent();
                } else if (auto* bs = dynamic_cast<materials::BeamSplitter*>(mat_ptr.get())) {
                    float refl = static_cast<float>(bs->reflectance());
                    float absn = static_cast<float>(bs->absorptance());
                    ImGui::Indent();

                    if (ImGui::SliderFloat("Reflected fraction##bs", &refl, 0.0f, 1.0f))
                        set_param(mat_id, "reflectance", refl);
                    tip("The share of light this splitter sends back, at every angle. A "
                        "50:50 splitter is 0.5; a 90:10 pickoff reflects 0.1.\n\n"
                        "What is neither reflected nor absorbed goes straight through with no "
                        "bend and no offset: exact for a pellicle, an approximation for a "
                        "plate, which would also make a faint ghost from its second face.");

                    if (ImGui::SliderFloat("Absorbed fraction##bs", &absn, 0.0f, 0.2f))
                        set_param(mat_id, "absorptance", absn);
                    tip("Light lost in the coating. Real splitters lose 1-5%. The slider "
                        "refuses a value that would make reflected + absorbed exceed 1.");

                    ImGui::Text("Transmitted: %.0f%%", 100.0 * bs->transmittance());
                    ImGui::Unindent();
                } else if (auto* po = dynamic_cast<materials::Polariser*>(mat_ptr.get())) {
                    float axis = static_cast<float>(po->axis_deg());
                    float er   = static_cast<float>(po->extinction_ratio());
                    float k1   = static_cast<float>(po->transmission());
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Transmission axis (deg)##po", &axis, -90.0f, 90.0f, "%.1f"))
                        set_param(mat_id, "transmission_axis_deg", axis);
                    tip("Angle of the pass axis in the part's own plane, from its local X. Turning "
                        "the part turns the axis with it. Two polarisers 90 degrees apart pass "
                        "almost nothing: Malus's law, cos^2 of the angle between them.");
                    if (ImGui::SliderFloat("Extinction ratio##po", &er, 1.0f, 1.0e6f, "%.0f",
                                           ImGuiSliderFlags_Logarithmic))
                        set_param(mat_id, "extinction_ratio", er);
                    tip("How much more the pass axis transmits than the crossed axis. Sheet film "
                        "is about 1000; a calcite polariser 100000.");
                    if (ImGui::SliderFloat("Pass-axis transmission##po", &k1, 0.0f, 1.0f, "%.2f"))
                        set_param(mat_id, "transmission", k1);
                    tip("Transmittance for light already along the axis. 1 is ideal (passes 50% of "
                        "unpolarised light); sheet polariser is about 0.77, which passes 38%.");
                    ImGui::Unindent();
                } else if (auto* wp = dynamic_cast<materials::Waveplate*>(mat_ptr.get())) {
                    float ret  = static_cast<float>(wp->retardance_waves());
                    float axis = static_cast<float>(wp->fast_axis_deg());
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Retardance (waves)##wp", &ret, 0.0f, 1.0f, "%.3f"))
                        set_param(mat_id, "retardance_waves", ret);
                    tip("0.25 is a quarter-wave plate (linear at 45 degrees to the fast axis becomes "
                        "circular), 0.5 a half-wave plate (rotates linear light by twice the angle). "
                        "The same at every wavelength here: a zero-order plate at its design "
                        "wavelength.");
                    if (ImGui::SliderFloat("Fast axis (deg)##wp", &axis, -90.0f, 90.0f, "%.1f"))
                        set_param(mat_id, "fast_axis_deg", axis);
                    tip("Angle of the fast axis in the part's own plane, from its local X.");
                    ImGui::Unindent();
                } else if (auto* pbs = dynamic_cast<materials::PolarisingBeamSplitter*>(mat_ptr.get())) {
                    float er = static_cast<float>(pbs->extinction_ratio());
                    ImGui::Indent();
                    if (ImGui::SliderFloat("Extinction ratio##pbs", &er, 2.0f, 1.0e5f, "%.0f",
                                           ImGuiSliderFlags_Logarithmic))
                        set_param(mat_id, "extinction_ratio", er);
                    tip("Transmits p and reflects s. 1/ER of each leaks into the wrong arm; a good "
                        "cube is about 1000. Unpolarised light splits exactly 50:50.");
                    ImGui::Unindent();
                }

                ImGui::PopID();
                ImGui::Separator();
            }
        }
    }
}

} // namespace scrt::viz
