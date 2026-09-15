#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/Layout.hpp"
#include "scrt/viz/ViewSettings.hpp"
#include "scrt/io/ResultsExporter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

#include "imgui.h"
#include "implot.h"

namespace scrt::viz {

namespace {

/// DNI of the scene on screen, published each frame by draw_sun_panel(). GUI thread only.
double g_scene_dni_wm2 = 1000.0;
/// False until a real scene DNI has been published; the window says so when it is false.
bool   g_scene_dni_known = false;
bool   g_scene_has_sun   = true;

/// Grey explanatory line, wrapped to the panel width; for plain-language captions.
void help_line(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

/// A headline figure with its explanation behind an info icon.
///
/// The explanations used to be printed under every number as a wrapped grey paragraph. Three of
/// those filled the column and left the flux map itself a 40px strip - the one thing the panel
/// exists to show. The words are worth keeping for a first-time reader; the space is not.
void figure(const char* value, const char* help) {
    ImGui::TextUnformatted(value);
    ImGui::SameLine();
    ImGui::TextDisabled(ICON_FA_CIRCLE_INFO);
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 22.0f);
        ImGui::TextUnformatted(help);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

} // namespace

void FluxPlotter::set_scene_dni(double dni_wm2) {
    if (dni_wm2 > 0.0) {
        g_scene_dni_wm2   = dni_wm2;
        g_scene_dni_known = true;
        g_scene_has_sun   = true;
    }
}

void FluxPlotter::set_no_sun() {
    g_scene_has_sun   = false;
    g_scene_dni_known = false;
}

bool FluxPlotter::scene_has_sun() { return g_scene_has_sun; }

double FluxPlotter::scene_dni() { return g_scene_dni_wm2; }

bool FluxPlotter::scene_dni_known() { return g_scene_dni_known; }

void FluxPlotter::draw(const tracer::FluxAccumulator& acc,
                       const tracer::TraceResult&     result,
                       double                         dni_wm2) {
    // A non-positive argument means "the call site cannot see the scene"; fall back to the
    // DNI the sun panel published this frame rather than to a hardcoded 1000 W/m^2.
    const double dni = (dni_wm2 > 0.0) ? dni_wm2 : scene_dni();

    // Position and size are the Viewer's job: it owns the docked layout and applies
    // SetNextWindowPos/Size before calling this. Setting them here as well would override
    // that, and the FirstUseEver they used to carry is exactly why this window ignored a
    // resize and sat wherever it was last dragged.
    ImGui::Begin("Flux Analysis", nullptr, docked_panel_flags());

    if (ImGui::BeginTabBar("flux_tabs")) {
        if (ImGui::BeginTabItem("Heatmap")) {
            draw_heatmap_tab(acc, dni);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Profiles")) {
            draw_profile_tab(acc, result);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    // Export row, collapsed by default: it is used once a session and was costing the plot
    // above it four rows of permanent vertical space.
    ImGui::Separator();
    if (!ImGui::CollapsingHeader(ICON_FA_FILE_EXPORT "  Export results")) { ImGui::End(); return; }
    help_line("Save the numbers: CSV is one row per patch of the pot, JSON is the summary.");
    static char out_csv[256] = "flux.csv";
    static char out_json[256] = "summary.json";
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##csv_path", out_csv, sizeof(out_csv));
    ImGui::SameLine();
    if (ImGui::Button("Export CSV")) {
        try { scrt::io::export_flux_csv(acc, out_csv); }
        catch (...) {}
    }
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("##json_path", out_json, sizeof(out_json));
    ImGui::SameLine();
    if (ImGui::Button("Export JSON")) {
        // The scene's real DNI, not a hardcoded 1000: it sets concentration_ratio in the
        // exported summary, so a hardcoded value writes a fabricated figure to disk.
        try {
            scrt::io::export_summary_json(
                acc, result, scene_has_sun() ? std::optional<double>(dni) : std::nullopt,
                out_json);
        }
        catch (...) {}
    }

    ImGui::End();
}

void FluxPlotter::draw_heatmap_tab(const tracer::FluxAccumulator& acc, double dni_wm2) {
    const auto& flux = acc.flux_map_wm2();
    if (flux.empty()) {
        ImGui::TextDisabled("No flux data — run a trace first.");
        return;
    }

    const int nx = acc.nx(), ny = acc.ny();

    // Smooth for display only, with the same sigma the 3D receiver uses. The scale below is
    // taken from the SMOOTHED array, because a blur lowers the peak and colouring a blurred map
    // against the raw peak would leave it permanently dim; the headline figures above still
    // report the raw values.
    const double sigma = static_cast<double>(flux_smoothing_sigma());
    const std::vector<double> shown = gaussian_smooth(flux, nx, ny, sigma);

    std::vector<float> data(shown.size());
    for (std::size_t i = 0; i < shown.size(); ++i)
        data[i] = static_cast<float>(shown[i]);

    // Taken over the FLOATS that are actually handed to ImPlot, not the doubles they came from.
    // ImPlot maps a value to a colour with (siz-1)*t + 0.5 and does not clamp the result, so a
    // sample even a rounding step above scale_max indexes past the end of the colour table - and
    // (float)x can round up above the double max of the same array.
    float vmaxf = 0.0f;
    for (float v : data) vmaxf = std::max(vmaxf, v);
    double vmax = static_cast<double>(vmaxf);
    if (vmax <= 0.0) vmax = 1.0;

    // ---- Headline figures, explanations on hover --------
    char buf[128];

    // Milliwatts below one watt: a 5 mW laser used to read "0 W" here.
    if (acc.total_power_w() < 1.0)
        std::snprintf(buf, sizeof(buf), "Power reaching the target: %.2f mW",
                      acc.total_power_w() * 1e3);
    else
        std::snprintf(buf, sizeof(buf), "Power reaching the target: %.0f W", acc.total_power_w());
    figure(buf, "The sunlight the cooker actually delivers to the target, after every "
                "reflection and loss. A 1 kW electric hob is 1000 W.");

    std::snprintf(buf, sizeof(buf), "Hottest spot: %.0f W/m²", acc.peak_flux_wm2());
    figure(buf, "Intensity where the light is most tightly focused. Bare midday sun is "
                "around 1000 W/m².");

    if (scene_has_sun()) {
        std::snprintf(buf, sizeof(buf), "Concentration: %.1fx (at %.0f W/m² sunlight)",
                      acc.concentration_ratio(dni_wm2), dni_wm2);
        figure(buf, (scene_dni_known() || dni_wm2 > 0.0)
                        ? "How many suns' worth of intensity the hot spot sees. Follows the DNI "
                          "slider on the Simulate tab."
                        : "No scene sunlight strength known yet; 1000 W/m² assumed.");

        ImGui::TextDisabled("Exact: %.6f W   %.6f W/m²   CR %.6f",
                            acc.total_power_w(), acc.peak_flux_wm2(),
                            acc.concentration_ratio(dni_wm2));
    } else {
        figure("Concentration: not defined (no sun in this scene)",
               "Concentration is the hottest spot divided by the sun's DNI. This scene is lit "
               "by something other than a sun, so there is no DNI to divide by.");
        ImGui::TextDisabled("Exact: %.6f W   %.6f W/m²",
                            acc.total_power_w(), acc.peak_flux_wm2());
    }

    ImGui::Separator();

    // Polyscope's own ramp, sampled - so the plot and the receiver in the 3D view are the same
    // colours rather than two libraries' idea of "viridis".
    ImPlot::PushColormap(implot_flux_colormap());
    const char* title = (sigma > 0.0f) ? "Where the light lands (smoothed for viewing)"
                                       : "Where the light lands (bright = hot)";
    if (ImPlot::BeginPlot(title, ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxis(ImAxis_X1, "x (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "y (m)");
        double hw = acc.half_width(), hh = acc.half_height();
        ImPlot::PlotHeatmap("flux", data.data(), ny, nx,
                            0.0, vmax, nullptr,
                            ImPlotPoint(-hw, -hh),
                            ImPlotPoint( hw,  hh));
        ImPlot::EndPlot();
    }
    ImPlot::PopColormap();
}

void FluxPlotter::draw_profile_tab(const tracer::FluxAccumulator& acc,
                                   const tracer::TraceResult& result) {
    const auto& flux = acc.flux_map_wm2();
    if (flux.empty()) {
        ImGui::TextDisabled("No flux data — run a trace first.");
        return;
    }
    const int nx = acc.nx(), ny = acc.ny();

    // Find peak bin
    auto it  = std::max_element(flux.begin(), flux.end());
    int peak = static_cast<int>(std::distance(flux.begin(), it));
    int pi   = peak % nx;
    int pj   = peak / nx;

    double bw = acc.bin_width_m(), bh = acc.bin_height_m();
    double hw = acc.half_width(),  hh = acc.half_height();

    // Horizontal profile through peak row pj
    std::vector<float> hx(nx), hy(nx);
    for (int i = 0; i < nx; ++i) {
        hx[i] = static_cast<float>(-hw + (i + 0.5) * bw);
        hy[i] = static_cast<float>(flux[pj * nx + i]);
    }

    // Vertical profile through peak column pi
    std::vector<float> vx(ny), vy(ny);
    for (int j = 0; j < ny; ++j) {
        vx[j] = static_cast<float>(-hh + (j + 0.5) * bh);
        vy[j] = static_cast<float>(flux[j * nx + pi]);
    }

    help_line("Slices straight through the brightest point, so you can see how wide the "
              "hot spot is and whether it lands where the pot sits.");

    ImGui::Text("Calculation took %.2f s   Sunbeams simulated: %zu   Landed on a surface: %zu",
                result.wall_time_s,
                result.primary_rays_traced,
                result.total_hits);

    if (ImPlot::BeginPlot("Brightness across the hot spot (left to right)", ImVec2(-1, 200))) {
        ImPlot::SetupAxis(ImAxis_X1, "x (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "W/m²");
        ImPlot::PlotLine("H-profile", hx.data(), hy.data(), nx);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Brightness across the hot spot (front to back)", ImVec2(-1, 200))) {
        ImPlot::SetupAxis(ImAxis_X1, "y (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "W/m²");
        ImPlot::PlotLine("V-profile", vx.data(), vy.data(), ny);
        ImPlot::EndPlot();
    }
}

} // namespace scrt::viz
