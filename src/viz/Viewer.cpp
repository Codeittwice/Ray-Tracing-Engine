#include "scrt/viz/Viewer.hpp"
#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/viz/Panels.hpp"
#include "scrt/viz/RayRenderer.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"
#include "implot.h"
#include "polyscope/polyscope.h"
#include "polyscope/surface_mesh.h"

namespace scrt::viz {

namespace {

void register_receiver_face_flux(const scene::ReceiverFace& face,
                                 const tracer::FluxAccumulator& acc,
                                 const std::string& mesh_name) {
    const int nx = acc.nx(), ny = acc.ny();
    const double hw = acc.half_width(), hh = acc.half_height();
    const auto& xf = face.surface()->transform();
    const auto& flux = acc.flux_map_wm2();

    std::vector<std::array<double, 3>>        pv;
    std::vector<std::array<std::uint32_t, 3>> pf;
    std::vector<double>                        fvals;

    pv.reserve(static_cast<std::size_t>((nx + 1) * (ny + 1)));
    fvals.reserve(static_cast<std::size_t>((nx + 1) * (ny + 1)));

    for (int j = 0; j <= ny; ++j) {
        double v = -hh + j * (2.0 * hh / ny);
        for (int i = 0; i <= nx; ++i) {
            double u = -hw + i * (2.0 * hw / nx);
            auto p = xf.point_to_world({u, v, 0.0});
            pv.push_back({p.x, p.y, p.z});

            double fval = 0.0;
            int cnt = 0;
            for (int dj : {-1, 0}) {
                for (int di : {-1, 0}) {
                    int bi = i + di;
                    int bj = j + dj;
                    if (bi >= 0 && bi < nx && bj >= 0 && bj < ny) {
                        fval += flux[static_cast<std::size_t>(bj * nx + bi)];
                        ++cnt;
                    }
                }
            }
            fvals.push_back(cnt > 0 ? fval / cnt : 0.0);
        }
    }

    pf.reserve(static_cast<std::size_t>(2 * nx * ny));
    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            auto v00 = static_cast<std::uint32_t>(j * (nx + 1) + i);
            auto v10 = static_cast<std::uint32_t>(j * (nx + 1) + i + 1);
            auto v01 = static_cast<std::uint32_t>((j + 1) * (nx + 1) + i);
            auto v11 = static_cast<std::uint32_t>((j + 1) * (nx + 1) + i + 1);
            pf.push_back({v00, v10, v11});
            pf.push_back({v00, v11, v01});
        }
    }

    auto* mesh = polyscope::registerSurfaceMesh(mesh_name, pv, pf);
    auto* q = mesh->addVertexScalarQuantity("flux_Wm2", fvals);
    q->setColorMap("viridis");
    q->setEnabled(true);
}

} // namespace

// ---- set_scene ---------------------------------------------------------------

void Viewer::set_scene(scene::Scene* s) {
    scene_ = s;
    if (s && s->receiver()) {
        auto& ra = s->receiver()->accumulator();
        acc_     = std::make_unique<tracer::FluxAccumulator>(
            ra.half_width(), ra.half_height(), ra.nx(), ra.ny());
    }
}

// ---- set_examples_dir --------------------------------------------------------

void Viewer::set_loaded_scene(io::LoadedScene ls, std::filesystem::path scene_dir) {
    scene_dir_ = std::move(scene_dir);
    load_scene_internal(std::move(ls));
}

void Viewer::set_examples_dir(std::filesystem::path dir) {
    examples_dir_ = std::move(dir);
    scan_examples_dir();
}

// ---- scan_examples_dir -------------------------------------------------------

void Viewer::scan_examples_dir() {
    available_scenes_.clear();
    scene_display_names_.clear();
    if (!std::filesystem::exists(examples_dir_)) return;
    for (const auto& e : std::filesystem::directory_iterator(examples_dir_))
        if (e.path().extension() == ".json")
            available_scenes_.push_back(e.path());
    std::sort(available_scenes_.begin(), available_scenes_.end());
    for (const auto& p : available_scenes_)
        scene_display_names_.push_back(p.filename().string());
}

// ---- load_from_file ----------------------------------------------------------

void Viewer::load_from_file(const std::filesystem::path& path) {
    auto ls = io::load_scene(path);
    load_scene_internal(std::move(ls));
}

// ---- load_scene_internal -----------------------------------------------------

void Viewer::load_scene_internal(io::LoadedScene ls) {
    ls.scene->build_acceleration_structure();
    ls.cfg.record_paths        = true;
    ls.cfg.max_paths_to_record = 200;
    // The editor takes ownership of both the scene and the document it was built from,
    // so every runtime mutation keeps the two in step. scene_ is a view into it.
    const tracer::TraceConfig loaded_cfg = ls.cfg;
    editor_ = std::make_unique<scene::SceneEditor>(std::move(ls), scene_dir_);
    owned_scene_.reset();

    traced_       = false;
    need_retrace_ = false;
    need_rebuild_ = false;

    polyscope::removeAllStructures();
    set_scene(&editor_->scene());
    cfg_ = loaded_cfg;
    init_surf_xforms();
    register_scene();
    run_trace(10'000);
}

// ---- init_surf_xforms --------------------------------------------------------

void Viewer::init_surf_xforms() {
    edits_.clear();
    if (!scene_) return;
    for (const auto& s : scene_->surfaces()) {
        ObjectEditState st;
        st.base = s->transform();
        edits_[s->id()] = st;
    }
    need_rebuild_ = false;
}

// ---- run_trace ---------------------------------------------------------------

void Viewer::run_trace(std::size_t n_rays) {
    if (!scene_) return;
    auto* recv = scene_->receiver();
    if (!recv) return;

    if (need_rebuild_) {
        scene_->build_acceleration_structure();
        need_rebuild_ = false;
    }

    // Fresh accumulator (zeroed) for this run
    auto& ra = recv->accumulator();
    acc_     = std::make_unique<tracer::FluxAccumulator>(
        ra.half_width(), ra.half_height(), ra.nx(), ra.ny());

    cfg_.n_primary_rays = n_rays;

    tracer::Tracer tracer(*scene_);
    if (recv->is_multi_face()) {
        result_ = tracer.run(cfg_);
        acc_ = std::make_unique<tracer::FluxAccumulator>(recv->accumulator());
    } else {
        result_ = tracer.run(cfg_, *acc_);
    }
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
    if (!scene_ || !scene_->receiver()) return;

    auto* recv = scene_->receiver();
    if (recv->is_multi_face()) {
        for (const auto& face : recv->faces()) {
            register_receiver_face_flux(
                *face, face->accumulator(), "receiver_" + face->name() + "_flux");
        }
        return;
    }

    if (!acc_) return;
    register_receiver_face_flux(*recv->faces().front(), *acc_, "receiver_flux");
}

// ---- make_panel_context --------------------------------------------------------

PanelContext Viewer::make_panel_context() {
    PanelContext ctx;
    ctx.scene               = scene_;
    ctx.cfg                 = &cfg_;
    ctx.result              = &result_;
    ctx.acc                 = acc_.get();
    ctx.traced              = traced_;
    ctx.need_retrace         = &need_retrace_;
    ctx.need_rebuild         = &need_rebuild_;

    ctx.available_scenes     = &available_scenes_;
    ctx.scene_display_names  = &scene_display_names_;
    ctx.selected_scene_idx   = &selected_scene_idx_;
    ctx.load_error           = &load_error_;

    ctx.edits                = &edits_;
    ctx.selected_id          = &selected_id_;

    ctx.load_scene           = [this](const std::filesystem::path& path) { load_from_file(path); };
    ctx.run_trace             = [this](std::size_t n) { run_trace(n); };

    // Import needs the scene's own directory so mesh paths stay relative to the file,
    // and a SceneEditor so a committed element reaches both document and live scene.
    ctx.scene_dir             = &scene_dir_;
    if (editor_) {
        ctx.add_element = [this](io::ElementDoc d) {
            const std::uint64_t id = editor_->add_element(std::move(d));
            need_rebuild_ = true;
            need_retrace_ = true;
            return id;
        };
    }

    return ctx;
}

// ---- draw_gui ----------------------------------------------------------------

void Viewer::draw_gui() {
    ImGui::SetNextWindowSize(ImVec2(300, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Solar Cooker RT");

    PanelContext ctx = make_panel_context();
    draw_outliner_panel(ctx);
    draw_scene_browser_panel(ctx);
    draw_transform_panel(ctx);
    draw_materials_panel(ctx);
    draw_sun_panel(ctx);
    draw_trace_panel(ctx);
    draw_import_panel(ctx);

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

    init_surf_xforms();
    register_scene();

    // Quick preview
    auto prev_cfg                = cfg_;
    prev_cfg.n_primary_rays      = 10'000;
    prev_cfg.record_paths        = true;
    prev_cfg.max_paths_to_record = 200;
    cfg_                         = prev_cfg;
    run_trace(10'000);
    cfg_                         = prev_cfg;

    polyscope::state::userCallback = [this]() { draw_gui(); };
    polyscope::show();

    ImPlot::DestroyContext();
}

} // namespace scrt::viz
