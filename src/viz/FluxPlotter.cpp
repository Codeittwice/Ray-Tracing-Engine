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
/// Total source power of the scene on screen, for the fixed flux reference. GUI thread only.
double g_scene_source_power_w = 0.0;

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

void   FluxPlotter::set_scene_source_power(double watts) { g_scene_source_power_w = watts; }
double FluxPlotter::scene_source_power() { return g_scene_source_power_w; }

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

    // Block-average down to at most kMaxCellsPerSide cells a side for DRAWING. ImPlot draws each
    // heatmap cell as its own quad into one ImGui draw list with 16-bit indices, so more than
    // 65536 vertices (about 128x128 cells) is an assertion in debug and garbage in release. A
    // 200x200 laser receiver hit exactly that. Averaging keeps the picture honest - the mean flux of
    // each block - and every stated number above and below still comes from the raw bins.
    constexpr int kMaxCellsPerSide = 120;
    const int     k   = std::max(1, (std::max(nx, ny) + kMaxCellsPerSide - 1) / kMaxCellsPerSide);
    const int     dnx = (nx + k - 1) / k;
    const int     dny = (ny + k - 1) / k;
    std::vector<float> data(static_cast<std::size_t>(dnx) * static_cast<std::size_t>(dny), 0.0f);
    for (int bj = 0; bj < dny; ++bj) {
        for (int bi = 0; bi < dnx; ++bi) {
            double sum = 0.0;
            int    cnt = 0;
            for (int j = bj * k; j < std::min(ny, (bj + 1) * k); ++j)
                for (int i = bi * k; i < std::min(nx, (bi + 1) * k); ++i) {
                    sum += shown[static_cast<std::size_t>(j) * static_cast<std::size_t>(nx) +
                                 static_cast<std::size_t>(i)];
                    ++cnt;
                }
            data[static_cast<std::size_t>(bj) * static_cast<std::size_t>(dnx) +
                 static_cast<std::size_t>(bi)] = cnt > 0 ? static_cast<float>(sum / cnt) : 0.0f;
        }
    }

    // Taken over the FLOATS that are actually handed to ImPlot, not the doubles they came from.
    // ImPlot maps a value to a colour with (siz-1)*t + 0.5 and does not clamp the result, so a
    // sample even a rounding step above scale_max indexes past the end of the colour table - and
    // (float)x can round up above the double max of the same array.
    // The colour scale is FIXED, from the source (the user's choice): 100% = total source power over
    // the receiver area, divided by the sensitivity. It never adapts to this trace, so a design that
    // delivers less power visibly dims. Values above full scale are clamped for drawing - ImPlot maps
    // a value to a colour without clamping, and a sample above scale_max reads past its colour table.
    const double reference = flux_reference_wm2(scene_source_power(), acc.half_width(), acc.half_height());
    const double sens      = static_cast<double>(flux_sensitivity());
    float        vmaxf     = 0.0f;
    if (reference > 0.0) {
        vmaxf = static_cast<float>(reference / sens);
        for (float& v : data) v = std::min(v, vmaxf);
    } else {
        for (float v : data) vmaxf = std::max(vmaxf, v);   // no power known: fall back to the peak
    }
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

    // ---- the fixed scale ----
    if (reference > 0.0) {
        std::snprintf(buf, sizeof(buf), "Full scale: %.0f%% of %.4g W/m² (source power / screen area)",
                      100.0 / sens, reference);
        figure(buf, "The colours are on a FIXED scale taken from the light source: 100% is the flux "
                    "you would get if every watt the sources emit fell evenly on this screen. It does "
                    "not change from one trace to the next, so a design that delivers less power is "
                    "visibly darker. A focused spot can read far above 100% - raise the sensitivity "
                    "to see dim light, lower it to see inside a bright spot.");
        std::snprintf(buf, sizeof(buf), "Hottest spot: %.1f%% of reference", 100.0 * acc.peak_flux_wm2() / reference);
        ImGui::TextDisabled("%s", buf);
        // Measured on QA 11: three 2 mm spots on a 16 mm screen peak at 6024% of the reference, so at
        // x1 they all saturate and look alike. The slider reaches x0.0001 for exactly that case.
        float s = flux_sensitivity();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
        if (ImGui::SliderFloat("Sensitivity##flux", &s, 1.0e-4f, 1000.0f, "x%.3g", ImGuiSliderFlags_Logarithmic))
            set_flux_sensitivity(s);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Full scale = reference / sensitivity. x1: the reference is full scale;\n"
                              "x10: a tenth of it is. Changes the picture only, never a number.");
    }

    // Polyscope's own ramp, sampled - so the plot and the receiver in the 3D view are the same
    // colours rather than two libraries' idea of "viridis".
    ImPlot::PushColormap(implot_flux_colormap());
    const char* title = (sigma > 0.0f) ? "Where the light lands (smoothed for viewing)"
                                       : "Where the light lands (bright = hot)";
    // A colour bar in PERCENT of the reference, so "how much less" is readable straight off the map.
    if (reference > 0.0) {
        const float bar_w = ImGui::GetFontSize() * 4.0f;
        const float h     = std::max(80.0f, ImGui::GetContentRegionAvail().y);
        ImPlot::ColormapScale("% of reference", 0.0, 100.0 / sens, ImVec2(bar_w, h), "%.0f%%");
        ImGui::SameLine();
    }
    if (ImPlot::BeginPlot(title, ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxis(ImAxis_X1, "x (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "y (m)");
        double hw = acc.half_width(), hh = acc.half_height();
        ImPlot::PlotHeatmap("flux", data.data(), dny, dnx,
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
