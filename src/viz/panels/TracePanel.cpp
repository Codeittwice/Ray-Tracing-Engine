#include "scrt/viz/Panels.hpp"

#include <cstdio>

#include "imgui.h"

namespace scrt::viz {

/// Draws the trace controls and results panel.
void draw_trace_panel(PanelContext& ctx) {
    // ---- Trace controls --
    if (ImGui::CollapsingHeader("Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
        int nr = static_cast<int>(ctx.cfg->n_primary_rays);
        ImGui::SliderInt("Rays", &nr, 1000, 10'000'000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ctx.cfg->n_primary_rays = static_cast<std::size_t>(nr);

        int mb = ctx.cfg->max_bounces;
        ImGui::SliderInt("Max bounces", &mb, 1, 32);
        ctx.cfg->max_bounces = mb;

        bool rec = ctx.cfg->record_paths;
        if (ImGui::Checkbox("Record paths", &rec))
            ctx.cfg->record_paths = rec;

        if (ctx.need_retrace && *ctx.need_retrace)
            ImGui::TextColored({1, 0.6f, 0, 1}, "Parameters changed");

        // Both launch buttons are dead while a trace runs: the worker reads the scene without
        // a lock, and a second run would race the first for it.
        ImGui::BeginDisabled(ctx.trace_running);
        if (ImGui::Button("Preview (10k rays)")) {
            if (ctx.run_trace) ctx.run_trace(10'000);
        }
        ImGui::SameLine();
        if (ImGui::Button("Full Trace")) {
            if (ctx.run_trace) ctx.run_trace(ctx.cfg->n_primary_rays);
        }
        ImGui::EndDisabled();

        // ---- progress + cancel (only while a worker is running) --
        if (ctx.trace_running) {
            const float frac =
                ctx.trace_rays_total > 0
                    ? static_cast<float>(static_cast<double>(ctx.trace_rays_done) /
                                         static_cast<double>(ctx.trace_rays_total))
                    : 0.0f;
            char overlay[64];
            std::snprintf(overlay, sizeof(overlay), "%llu / %llu rays",
                          static_cast<unsigned long long>(ctx.trace_rays_done),
                          static_cast<unsigned long long>(ctx.trace_rays_total));
            ImGui::ProgressBar(frac < 0.f ? 0.f : (frac > 1.f ? 1.f : frac), ImVec2(-1, 0),
                               overlay);
            if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
                if (ctx.cancel_trace) ctx.cancel_trace();
            }
            ImGui::TextDisabled("Tracing… the scene is locked until this finishes.");
        }
    }

    // ---- Results --
    if (ctx.traced && ctx.acc) {
        if (ImGui::CollapsingHeader("Results", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Total power : %.3f W", ctx.acc->total_power_w());
            ImGui::Text("Peak flux   : %.1f W/m²", ctx.acc->peak_flux_wm2());
            ImGui::Text("Concentration : %.1f×",
                        ctx.acc->concentration_ratio(
                            (ctx.scene && ctx.scene->sun()) ? ctx.scene->sun()->dni() : 1000.0));
            ImGui::Text("Wall time   : %.2f s", ctx.result->wall_time_s);
            ImGui::Text("Rays/s      : %.1f k",
                        ctx.result->primary_rays_traced / ctx.result->wall_time_s / 1e3);
        }
    }
}

} // namespace scrt::viz
