#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/io/ResultsExporter.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui.h"
#include "implot.h"

namespace scrt::viz {

void FluxPlotter::draw(const tracer::FluxAccumulator& acc,
                       const tracer::TraceResult&     result) {
    ImGui::SetNextWindowSize(ImVec2(520, 480), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(820, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Flux Analysis");

    if (ImGui::BeginTabBar("flux_tabs")) {
        if (ImGui::BeginTabItem("Heatmap")) {
            draw_heatmap_tab(acc);
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
        try { scrt::io::export_summary_json(acc, result, 1000.0, out_json); }
        catch (...) {}
    }

    ImGui::End();
}

void FluxPlotter::draw_heatmap_tab(const tracer::FluxAccumulator& acc) {
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

    ImGui::Text("Peak: %.1f W/m²   Total: %.2f W   CR: %.1f×",
                acc.peak_flux_wm2(),
                acc.total_power_w(),
                acc.concentration_ratio(1000.0));

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

    ImGui::Text("Wall time: %.2f s   Rays: %zu   Hits: %zu",
                result.wall_time_s,
                result.primary_rays_traced,
                result.total_hits);

    if (ImPlot::BeginPlot("Horizontal profile (y = peak row)", ImVec2(-1, 200))) {
        ImPlot::SetupAxis(ImAxis_X1, "x (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "W/m²");
        ImPlot::PlotLine("H-profile", hx.data(), hy.data(), nx);
        ImPlot::EndPlot();
    }
    if (ImPlot::BeginPlot("Vertical profile (x = peak col)", ImVec2(-1, 200))) {
        ImPlot::SetupAxis(ImAxis_X1, "y (m)");
        ImPlot::SetupAxis(ImAxis_Y1, "W/m²");
        ImPlot::PlotLine("V-profile", vx.data(), vy.data(), ny);
        ImPlot::EndPlot();
    }
}

} // namespace scrt::viz
