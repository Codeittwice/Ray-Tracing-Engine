#include "scrt/viz/Panels.hpp"
#include "scrt/scene/SceneEditor.hpp"

#include <cstdio>

#include "imgui.h"

namespace scrt::viz {

namespace {

/// Mirrors the ray count and bounce limit into the document, so a save records what the user
/// chose to run rather than what the file was loaded with.
///
/// Deliberately only these two. The Viewer also sets `record_paths` and `max_paths_to_record`
/// on its own copy to drive the 3D ray display; those are view settings, not a property of the
/// scene, and writing them would dirty a document nobody has edited the moment the app opens.
void commit_trace(PanelContext& ctx) {
    if (!ctx.editor || !ctx.cfg) return;
    tracer::TraceConfig recorded = ctx.editor->doc().trace;
    recorded.n_primary_rays      = ctx.cfg->n_primary_rays;
    recorded.max_bounces         = ctx.cfg->max_bounces;
    ctx.editor->commit_trace_config(recorded);
}

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

} // namespace

/// Draws the trace controls and results panel.
void draw_trace_panel(PanelContext& ctx) {
    // ---- Trace controls --
    if (ImGui::CollapsingHeader("Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
        int nr = static_cast<int>(ctx.cfg->n_primary_rays);
        ImGui::SliderInt("Rays", &nr, 1000, 10'000'000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        tip("How many individual sunbeams to simulate.\n\n"
            "The app fires rays at random points across the collector and counts where "
            "they land, so the answer is an estimate that sharpens as you add rays. Few "
            "rays give a fast, speckled result; many give a smooth one and take longer. "
            "The noise falls as the square root of this number, so 100x the rays buys "
            "10x the smoothness.\n\n"
            "Use Preview while you are moving things around, then a full run for a "
            "number you intend to quote.");
        ctx.cfg->n_primary_rays = static_cast<std::size_t>(nr);
        commit_trace(ctx);

        int mb = ctx.cfg->max_bounces;
        ImGui::SliderInt("Max bounces", &mb, 1, 32);
        tip("How many times a single ray may reflect or pass through a surface before "
            "the app gives up on it.\n\n"
            "A simple dish needs 2 or 3: one bounce off the mirror, one into the pot. "
            "Deep box cookers and multi-mirror rigs need more. Set it too low and light "
            "that would really have arrived is discarded, quietly under-reporting the "
            "power; set it far higher than the design needs and you only pay in time.");
        ctx.cfg->max_bounces = mb;
        commit_trace(ctx);

        bool rec = ctx.cfg->record_paths;
        if (ImGui::Checkbox("Record paths", &rec))
            ctx.cfg->record_paths = rec;
        tip("Keep a sample of ray journeys so they can be drawn as lines in the 3D "
            "view.\n\n"
            "Useful for seeing where light is escaping or missing the pot. It costs "
            "memory and slows the trace, so leave it off for large runs.");

        if (ctx.need_retrace && *ctx.need_retrace)
            ImGui::TextColored({1, 0.6f, 0, 1}, "Parameters changed");

        // Both launch buttons are dead while a trace runs: the worker reads the scene without
        // a lock, and a second run would race the first for it.
        ImGui::BeginDisabled(ctx.trace_running);
        if (ImGui::Button("Preview (10k rays)")) {
            if (ctx.run_trace) ctx.run_trace(10'000);
        }
        tip("A fast, rough run at 10,000 rays — good enough to see whether the light is "
            "landing on the pot at all. Ignores the Rays slider.");
        ImGui::SameLine();
        if (ImGui::Button("Full Trace")) {
            if (ctx.run_trace) ctx.run_trace(ctx.cfg->n_primary_rays);
        }
        tip("Run with the full ray count set on the Rays slider. Runs in the background: "
            "you can watch the progress bar and cancel it.");
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
            tip("Stop the run now and keep whatever has been gathered so far. The "
                "partial result is noisier than a completed run, not wrong.");
            ImGui::TextDisabled("Tracing… the scene is locked until this finishes.");
        }
    }

    // ---- Results --
    if (ctx.traced && ctx.acc) {
        if (ImGui::CollapsingHeader("Results", ImGuiTreeNodeFlags_DefaultOpen)) {
            // The scene's real DNI, never a hardcoded 1000: the concentration ratio is
            // peak flux divided by it, so a stale literal here would misreport the ratio
            // by DNI/1000 the moment the Sun panel's slider moved.
            const double dni =
                (ctx.scene && ctx.scene->sun()) ? ctx.scene->sun()->dni() : 1000.0;

            // Plain-language names, with the physical quantity kept in parentheses and
            // the original precision unchanged. Nothing here is rounded more than before.
            ImGui::Text("Power reaching the pot : %.3f W", ctx.acc->total_power_w());
            tip("Total sunlight delivered to the target, after every reflection and "
                "loss along the way (total_power_w).\n\n"
                "Compare it to an electric hob: a small one is 1000 W. This is optical "
                "power arriving — it does not account for heat leaking back out of the "
                "pot, which this app does not model.");

            ImGui::Text("Hottest spot           : %.1f W/m²", ctx.acc->peak_flux_wm2());
            tip("Peak flux: the intensity in the single brightest cell of the target "
                "grid, in watts per square metre (peak_flux_wm2).\n\n"
                "Bare midday sun is around 1000 W/m². This is what decides whether a "
                "spot scorches, while the power above decides how fast the whole pot "
                "heats.");

            ImGui::Text("Concentration          : %.1f× (at %.0f W/m² sun)",
                        ctx.acc->concentration_ratio(dni), dni);
            tip("How many suns' worth of intensity the hottest spot sees: the peak flux "
                "divided by the DNI set in the Sun panel (concentration_ratio).\n\n"
                "10x means the brightest point is ten times as intense as unfocused "
                "sunlight. It follows the DNI slider, so it describes the optics rather "
                "than the weather.");

            ImGui::Text("Time taken             : %.2f s", ctx.result->wall_time_s);
            tip("Wall-clock seconds the trace took (wall_time_s).");

            // Guard the division: a trace fast enough to round to 0.00 s used to make
            // this line read "inf k" or "-nan(ind) k". Display only — nothing downstream
            // reads this figure.
            if (ctx.result->wall_time_s > 0.0) {
                ImGui::Text("Rays/s                 : %.1f k",
                            ctx.result->primary_rays_traced / ctx.result->wall_time_s / 1e3);
            } else {
                ImGui::Text("Rays/s                 : n/a (too fast to time)");
            }
            tip("Thousands of rays traced per second — a speed figure for this machine, "
                "not a property of the cooker.");

            ImGui::Separator();
            // Full precision, unrounded, for anyone transcribing a result. The readouts
            // above are the same numbers; this line exists so the plain-language framing
            // never costs a significant figure.
            ImGui::TextDisabled("Exact: total %.6f W   peak %.6f W/m²   CR %.6f×",
                                ctx.acc->total_power_w(), ctx.acc->peak_flux_wm2(),
                                ctx.acc->concentration_ratio(dni));
            help_line("Optical results only: this model tracks where light goes, not how "
                      "hot the pot gets or how fast it cools.");
        }
    }
}

} // namespace scrt::viz
