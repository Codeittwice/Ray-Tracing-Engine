#include "scrt/viz/Panels.hpp"
#include "scrt/viz/Icons.hpp"

#include "scrt/math/Constants.hpp"
#include "scrt/sources/Laser.hpp"
#include "scrt/surfaces/Surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "imgui.h"
#include "implot.h"

namespace scrt::viz {

namespace {

/// Setup choices, kept across frames and parts (tool state, not scene data).
struct SweepSetup {
    int    kind      = 0;       ///< 0 move, 1 turn.
    int    axis      = 0;       ///< 0 X, 1 Y, 2 Z.
    double from_ui   = 0.0;     ///< mm for move, degrees for turn.
    double to_ui     = 1.0;
    int    steps     = 40;
    int    rays      = 50000;
};
SweepSetup g_setup;

void tip(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("%s", text);
}

/// The first laser's wavelength [nm], or 0 when the scene has none.
double first_laser_nm(const PanelContext& ctx) {
    if (!ctx.scene) return 0.0;
    for (const auto& src : ctx.scene->sources())
        if (const auto* l = dynamic_cast<const sources::Laser*>(src.get())) return l->wavelength_nm();
    return 0.0;
}

} // namespace

void draw_sweep_setup(PanelContext& ctx) {
    if (!ctx.start_sweep) return;
    // In the Simulate tab rather than under the transform editor: there it sat below the fold of a
    // scrolling panel (seen on the first screenshot), and a sweep is a kind of trace.
    if (!ImGui::CollapsingHeader(ICON_FA_PLAY "  Sweep a part", ImGuiTreeNodeFlags_DefaultOpen)) return;
    const std::uint64_t     surface_id = ctx.selected_id ? *ctx.selected_id : 0;
    const surfaces::Surface* part      = (ctx.scene && surface_id) ? ctx.scene->surface_by_id(surface_id) : nullptr;
    if (!part) {
        ImGui::TextWrapped("Select a part (in the 3D view or the scene tree) to trace it through a range of "
                           "positions and play the result back.");
        return;
    }
    ImGui::TextWrapped("Trace %s through a range of positions and play the result back.", part->name().c_str());

    ImGui::RadioButton("Move##sw", &g_setup.kind, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Turn##sw", &g_setup.kind, 1);
    // Its own line: beside Move/Turn it ran off the column and clipped Z (first screenshot).
    ImGui::TextDisabled(g_setup.kind == 0 ? "along world" : "about world");
    ImGui::SameLine();
    ImGui::RadioButton("X##swa", &g_setup.axis, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Y##swa", &g_setup.axis, 1);
    ImGui::SameLine();
    ImGui::RadioButton("Z##swa", &g_setup.axis, 2);

    const bool        move = (g_setup.kind == 0);
    const char* const from_fmt = move ? "from %.6f mm" : "from %.4f deg";
    const char* const to_fmt   = move ? "to %.6f mm" : "to %.4f deg";
    const float       half = std::max(1.0f, (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) / 2.0f);
    ImGui::SetNextItemWidth(half);
    ImGui::InputDouble("##sw_from", &g_setup.from_ui, 0.0, 0.0, from_fmt);
    tip(move ? "Start offset from the part's current position, millimetres (6 decimals = 1 nm)."
             : "Start angle from the part's current orientation, degrees, about its own centre.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(half);
    ImGui::InputDouble("##sw_to", &g_setup.to_ui, 0.0, 0.0, to_fmt);
    tip("End offset. The part is put back where it was when the sweep is closed.");

    // The Michelson case, one click away: lambda/2 of mirror travel is one fringe.
    const double nm = first_laser_nm(ctx);
    if (move && nm > 0.0) {
        char label[96];
        std::snprintf(label, sizeof(label), "Range: 0 to lambda/2 (%.1f nm)", 0.5 * nm);
        if (ImGui::SmallButton(label)) {
            g_setup.from_ui = 0.0;
            g_setup.to_ui   = 0.5 * nm * 1e-6;   // nm -> mm
        }
        tip("Moving a mirror by half a wavelength changes that arm's round trip by one wavelength:\n"
            "on a coherent screen every fringe moves exactly one spacing.");
    }

    ImGui::SetNextItemWidth(half);
    ImGui::SliderInt("Steps##sw", &g_setup.steps, 2, 240);
    ImGui::SetNextItemWidth(half);
    ImGui::SliderInt("Rays / step##sw", &g_setup.rays, 1000, 1000000, "%d", ImGuiSliderFlags_Logarithmic);
    tip("Each step is a full trace at this many rays. Laser benches take 10-90 ms per step at\n"
        "100k-300k rays in the release build, so 40 steps is a few seconds.");

    if (ImGui::Button(ICON_FA_PLAY "  Run sweep", ImVec2(-1, 0))) {
        SweepSpec spec;
        spec.surface_id = surface_id;
        spec.kind       = move ? SweepSpec::Kind::Move : SweepSpec::Kind::Turn;
        spec.axis       = g_setup.axis;
        const double k  = move ? 1e-3 : math::PI / 180.0;   // mm -> m, deg -> rad
        spec.from       = g_setup.from_ui * k;
        spec.to         = g_setup.to_ui * k;
        spec.steps      = g_setup.steps;
        spec.rays       = static_cast<std::size_t>(g_setup.rays);
        ctx.start_sweep(spec);
    }
}

void draw_sweep_window(PanelContext& ctx, const ImVec2& vmin, const ImVec2& vmax) {
    const SweepRunner* sw = ctx.sweep;
    if (!sw || !ctx.sweep_busy) return;

    const float w = std::min(vmax.x - vmin.x - 20.0f, 720.0f * std::max(1.0f, ImGui::GetFontSize() / 16.0f));
    ImGui::SetNextWindowPos(ImVec2(0.5f * (vmin.x + vmax.x), vmax.y - 10.0f), ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowSize(ImVec2(w, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.92f);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_AlwaysAutoResize;
    if (!ImGui::Begin("##sweep_window", nullptr, flags)) { ImGui::End(); return; }

    const bool   move  = sw->spec().kind == SweepSpec::Kind::Move;
    const double scale = move ? 1e3 : 180.0 / math::PI;   // m -> mm, rad -> deg
    const char*  unit  = move ? "mm" : "deg";

    if (sw->running()) {
        const float frac = static_cast<float>(sw->frames_done()) / static_cast<float>(std::max(1, sw->frames_total()));
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "Sweeping: step %d of %d", sw->frames_done(), sw->frames_total());
        ImGui::ProgressBar(frac, ImVec2(-120.0f, 0.0f), overlay);
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP "  Cancel", ImVec2(-1, 0)) && ctx.stop_sweep) ctx.stop_sweep();
        ImGui::End();
        return;
    }

    const auto& frames = sw->frames();
    const int   n      = static_cast<int>(frames.size());
    if (n == 0) { ImGui::End(); return; }
    int i = std::clamp(*ctx.sweep_frame, 0, n - 1);

    const char* play = *ctx.sweep_playing ? ICON_FA_STOP "  Pause" : ICON_FA_PLAY "  Play";
    if (ImGui::Button(play)) *ctx.sweep_playing = !*ctx.sweep_playing;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 190.0f);
    if (ImGui::SliderInt("##sw_frame", &i, 0, n - 1, "frame %d")) {
        *ctx.sweep_playing = false;
        if (ctx.show_sweep_frame) ctx.show_sweep_frame(i);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_XMARK "  Close sweep", ImVec2(-1, 0)) && ctx.stop_sweep) ctx.stop_sweep();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Puts the part back where it was before the sweep and re-traces.");

    const SweepFrame& f = frames[static_cast<std::size_t>(i)];
    if (f.total_power_w < 1.0)
        ImGui::Text("offset %.6f %s   |   on target %.4g mW   |   screen centre %.4g W/m2", f.value * scale, unit,
                    f.total_power_w * 1e3, f.centre_wm2);
    else
        ImGui::Text("offset %.6f %s   |   on target %.4g W   |   screen centre %.4g W/m2", f.value * scale, unit,
                    f.total_power_w, f.centre_wm2);

    // Power on target and the centre intensity against the swept offset, with the current frame marked.
    std::vector<double> x(static_cast<std::size_t>(n)), pw(x.size()), ce(x.size());
    for (int k = 0; k < n; ++k) {
        x[static_cast<std::size_t>(k)]  = frames[static_cast<std::size_t>(k)].value * scale;
        pw[static_cast<std::size_t>(k)] = frames[static_cast<std::size_t>(k)].total_power_w * 1e3;
        ce[static_cast<std::size_t>(k)] = frames[static_cast<std::size_t>(k)].centre_wm2;
    }
    if (ImPlot::BeginPlot("##sweep_plot", ImVec2(-1, 170.0f * std::max(1.0f, ImGui::GetFontSize() / 16.0f)))) {
        ImPlot::SetupAxes(move ? "offset (mm)" : "angle (deg)", "on target (mW)", ImPlotAxisFlags_AutoFit,
                          ImPlotAxisFlags_AutoFit);
        ImPlot::SetupAxis(ImAxis_Y2, "centre (W/m2)", ImPlotAxisFlags_AuxDefault | ImPlotAxisFlags_AutoFit);
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
        // Thicker, underneath: when the two curves have the same shape (Malus: both cos^2) the centre
        // line lies exactly on top, and a same-width line underneath vanished (seen on QA 18).
        ImPlot::SetNextLineStyle(IMPLOT_AUTO_COL, 4.0f);
        ImPlot::PlotLine("on target", x.data(), pw.data(), n);
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        ImPlot::PlotLine("screen centre", x.data(), ce.data(), n);
        double cur = f.value * scale;
        ImPlot::DragLineX(0, &cur, ImVec4(1.0f, 0.62f, 0.12f, 1.0f), 1.5f, ImPlotDragToolFlags_NoInputs);
        ImPlot::EndPlot();
    }
    ImGui::End();
}

} // namespace scrt::viz
