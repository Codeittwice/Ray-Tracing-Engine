#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/io/ResultsExporter.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui.h"
#include "implot.h"

namespace scrt::viz {

namespace {

/// DNI of the scene on screen, published each frame by draw_sun_panel(). GUI thread only.
double g_scene_dni_wm2 = 1000.0;
/// False until a real scene DNI has been published; the window says so when it is false.
bool   g_scene_dni_known = false;

/// Grey explanatory line, wrapped to the panel width; for plain-language captions.
void help_line(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

} // namespace

void FluxPlotter::set_scene_dni(double dni_wm2) {
    if (dni_wm2 > 0.0) {
        g_scene_dni_wm2   = dni_wm2;
        g_scene_dni_known = true;
    }
}

double FluxPlotter::scene_dni() { return g_scene_dni_wm2; }

bool FluxPlotter::scene_dni_known() { return g_scene_dni_known; }

void FluxPlotter::draw(const tracer::FluxAccumulator& acc,
                       const tracer::TraceResult&     result,
                       double                         dni_wm2) {
    // A non-positive argument means "the call site cannot see the scene"; fall back to the
    // DNI the sun panel published this frame rather than to a hardcoded 1000 W/m^2.
    const double dni = (dni_wm2 > 0.0) ? dni_wm2 : scene_dni();

    ImGui::SetNextWindowSize(ImVec2(520, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(820, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Flux Analysis");

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

    // Export row
    ImGui::Separator();
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
        try { scrt::io::export_summary_json(acc, result, dni, out_json); }
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

    // Convert to float for ImPlot
    std::vector<float> data(flux.size());
    for (std::size_t i = 0; i < flux.size(); ++i)
        data[i] = static_cast<float>(flux[i]);

    double vmax = acc.peak_flux_wm2();
    if (vmax <= 0.0) vmax = 1.0;

    // ---- Plain-language summary, then the same numbers at full precision --------
    ImGui::Text("Power reaching the pot: %.0f W", acc.total_power_w());
    help_line("The sunlight the cooker actually delivers to the target, after every "
              "reflection and loss. A 1 kW electric hob is 1000 W.");

    ImGui::Text("Hottest spot: %.0f W/m²", acc.peak_flux_wm2());
    help_line("Intensity where the light is most tightly focused. Bare midday sun is "
              "around 1000 W/m².");

    ImGui::Text("Concentration: %.1f× (at %.0f W/m² sunlight)",
                acc.concentration_ratio(dni_wm2), dni_wm2);
    if (scene_dni_known() || dni_wm2 > 0.0) {
        help_line("How many suns' worth of intensity the hot spot sees. Follows the DNI "
                  "slider in the Sun panel.");
    } else {
        help_line("No scene sunlight strength known yet; 1000 W/m² assumed.");
    }

    ImGui::Separator();
    ImGui::Text("Exact: total %.6f W   peak %.6f W/m²   CR %.6f×",
                acc.total_power_w(), acc.peak_flux_wm2(),
                acc.concentration_ratio(dni_wm2));

    help_line("Map of where the light lands on the pot. Bright = hot.");
    if (ImPlot::BeginPlot("Flux map", ImVec2(-1, -1),
                          ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxis(ImAxis_X1, "x (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "y (m)");
        double hw = acc.half_width(), hh = acc.half_height();
        ImPlot::PlotHeatmap("flux", data.data(), ny, nx,
                            0.0, vmax, nullptr,
                            ImPlotPoint(-hw, -hh),
                            ImPlotPoint( hw,  hh));
        ImPlot::EndPlot();
    }
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
