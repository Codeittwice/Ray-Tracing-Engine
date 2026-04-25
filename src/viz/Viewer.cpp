#include "scrt/viz/Viewer.hpp"
#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/viz/RayRenderer.hpp"
#include "scrt/materials/Dielectric.hpp"
#include "scrt/materials/RealMirror.hpp"
#include "scrt/scene/Aperture.hpp"
#include "scrt/scene/Receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"
#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

namespace scrt::viz {

// ---- set_scene ---------------------------------------------------------------

void Viewer::set_scene(scene::Scene* s) {
    scene_ = s;
    if (s && s->receiver()) {
        auto& ra = s->receiver()->accumulator();
        acc_     = std::make_unique<tracer::FluxAccumulator>(
            ra.half_width(), ra.half_height(), ra.nx(), ra.ny());
    }
}

// ---- run_trace ---------------------------------------------------------------

void Viewer::run_trace(std::size_t n_rays) {
    if (!scene_) return;
    auto* recv = scene_->receiver();
    if (!recv) return;

    // Fresh accumulator (zeroed) for this run
    auto& ra = recv->accumulator();
    acc_     = std::make_unique<tracer::FluxAccumulator>(
        ra.half_width(), ra.half_height(), ra.nx(), ra.ny());

    cfg_.n_primary_rays = n_rays;

    tracer::Tracer tracer(*scene_);
    result_ = tracer.run(cfg_, *acc_);
    traced_       = true;
    need_retrace_ = false;

    update_receiver_flux();

    if (!result_.sampled_paths.empty()) {
        RayRenderer renderer(scene_);
        renderer.register_paths(result_);
    }
}

// ---- register_scene ----------------------------------------------------------

void Viewer::register_scene() {
    RayRenderer renderer(scene_);
    renderer.register_surfaces(32);
    renderer.register_aperture();
    update_receiver_flux(); // registers zeroed heatmap mesh
}

// ---- update_receiver_flux ----------------------------------------------------

void Viewer::update_receiver_flux() {
    if (!scene_ || !scene_->receiver() || !acc_) return;

    auto* recv     = scene_->receiver();
    const int nx   = acc_->nx(), ny = acc_->ny();
    const double hw = acc_->half_width(), hh = acc_->half_height();
    const auto& xf = recv->surface()->transform();
    const auto& flux = acc_->flux_map_wm2();

    // (nx+1)*(ny+1) vertex grid matching the flux bins
    std::vector<std::array<double, 3>>        pv;
    std::vector<std::array<std::uint32_t, 3>> pf;
    std::vector<double>                        fvals;

    pv.reserve(static_cast<std::size_t>((nx + 1) * (ny + 1)));
    fvals.reserve(static_cast<std::size_t>((nx + 1) * (ny + 1)));

    for (int j = 0; j <= ny; ++j) {
        double v = -hh + j * (2.0 * hh / ny);
        for (int i = 0; i <= nx; ++i) {
            double u = -hw + i * (2.0 * hw / nx);
            auto   p = xf.point_to_world({u, v, 0.0});
            pv.push_back({p.x, p.y, p.z});

            // Average of adjacent bins (corner interpolation)
            double fval = 0.0; int cnt = 0;
            for (int dj : {-1, 0})
                for (int di : {-1, 0}) {
                    int bi = i + di, bj = j + dj;
                    if (bi >= 0 && bi < nx && bj >= 0 && bj < ny) {
                        fval += flux[static_cast<std::size_t>(bj * nx + bi)];
                        ++cnt;
                    }
                }
            fvals.push_back(cnt > 0 ? fval / cnt : 0.0);
        }
    }

    pf.reserve(static_cast<std::size_t>(2 * nx * ny));
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            auto v00 = static_cast<std::uint32_t>( j      * (nx + 1) + i    );
            auto v10 = static_cast<std::uint32_t>( j      * (nx + 1) + i + 1);
            auto v01 = static_cast<std::uint32_t>((j + 1) * (nx + 1) + i    );
            auto v11 = static_cast<std::uint32_t>((j + 1) * (nx + 1) + i + 1);
            pf.push_back({v00, v10, v11});
            pf.push_back({v00, v11, v01});
        }
    }

    auto* mesh = polyscope::registerSurfaceMesh("receiver_flux", pv, pf);
    auto* q    = mesh->addVertexScalarQuantity("flux_Wm2", fvals);
    q->setColorMap("plasma");
    q->setEnabled(true);
}

// ---- draw_gui ----------------------------------------------------------------

void Viewer::draw_gui() {
    ImGui::SetNextWindowSize(ImVec2(300, 560), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Solar Cooker RT");

    // ---- Scene --
    if (ImGui::CollapsingHeader("Scene", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (auto& mat_ptr : scene_->mutable_materials()) {
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

            if (changed) need_retrace_ = true;
            ImGui::PopID();
            ImGui::Separator();
        }
    }

    // ---- Sun --
    if (ImGui::CollapsingHeader("Sun", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (scene_->sun()) {
            float dni = static_cast<float>(scene_->sun()->dni());
            if (ImGui::SliderFloat("DNI (W/m²)", &dni, 500.0f, 1500.0f)) {
                const_cast<sources::SunSource*>(scene_->sun())->set_dni(dni);
                need_retrace_ = true;
            }
        }
    }

    // ---- Trace controls --
    if (ImGui::CollapsingHeader("Trace", ImGuiTreeNodeFlags_DefaultOpen)) {
        int nr = static_cast<int>(cfg_.n_primary_rays);
        ImGui::SliderInt("Rays", &nr, 1000, 10'000'000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        cfg_.n_primary_rays = static_cast<std::size_t>(nr);

        int mb = cfg_.max_bounces;
        ImGui::SliderInt("Max bounces", &mb, 1, 32);
        cfg_.max_bounces = mb;

        bool rec = cfg_.record_paths;
        if (ImGui::Checkbox("Record paths", &rec))
            cfg_.record_paths = rec;

        if (need_retrace_)
            ImGui::TextColored({1, 0.6f, 0, 1}, "Parameters changed");

        if (ImGui::Button("Preview (10k rays)"))
            run_trace(10'000);
        ImGui::SameLine();
        if (ImGui::Button("Full Trace"))
            run_trace(cfg_.n_primary_rays);
    }

    // ---- Results --
    if (traced_ && acc_) {
        if (ImGui::CollapsingHeader("Results", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Total power : %.3f W", acc_->total_power_w());
            ImGui::Text("Peak flux   : %.1f W/m²", acc_->peak_flux_wm2());
            ImGui::Text("Concentration : %.1f×",
                        acc_->concentration_ratio(
                            scene_->sun() ? scene_->sun()->dni() : 1000.0));
            ImGui::Text("Wall time   : %.2f s", result_.wall_time_s);
            ImGui::Text("Rays/s      : %.1f k",
                        result_.primary_rays_traced / result_.wall_time_s / 1e3);
        }
    }

    ImGui::End();

    // ---- Flux Analysis window --
    if (traced_ && acc_) {
        static FluxPlotter plotter;
        plotter.draw(*acc_, result_);
    }
}

// ---- run ---------------------------------------------------------------------

void Viewer::run() {
    if (!scene_) return;

    polyscope::options::programName = "Solar Cooker Ray Tracer";
    polyscope::options::verbosity   = 0;
    polyscope::init();
    ImPlot::CreateContext();

    register_scene();

    // Quick preview
    auto prev_cfg      = cfg_;
    prev_cfg.n_primary_rays = 10'000;
    prev_cfg.record_paths   = true;
    prev_cfg.max_paths_to_record = 200;
    cfg_               = prev_cfg;
    run_trace(10'000);
    cfg_               = prev_cfg; // restore from scene file

    polyscope::state::userCallback = [this]() { draw_gui(); };
    polyscope::show();

    ImPlot::DestroyContext();
}

} // namespace scrt::viz
