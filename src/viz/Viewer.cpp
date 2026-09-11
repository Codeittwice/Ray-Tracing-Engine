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

#include "imgui.h"       // must precede ImGuizmo.h: ImGuizmo.h declares against ImGui types
#include "ImGuizmo.h"    // NOLINT(build/include_order)
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

// ---- lifetime ----------------------------------------------------------------

Viewer::~Viewer() {
    // The worker holds a raw pointer into the scene, which the members below are about to
    // destroy. Joining here is the only thing standing between that and a use-after-free.
    cancel_and_join_trace();
}

// ---- set_scene ---------------------------------------------------------------

void Viewer::set_scene(scene::Scene* s) {
    cancel_and_join_trace();  // no worker may survive the scene it is reading
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
    // Nothing below may run while a worker is reading the scene we are about to replace.
    cancel_and_join_trace();

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

    set_scene(&editor_->scene());
    cfg_ = loaded_cfg;

    // set_loaded_scene() is called from main() BEFORE run() calls polyscope::init(), and
    // every polyscope entry point throws "Polyscope has not been initialized" until then.
    // Returning here is complete rather than partial: run() does removeAllStructures,
    // init_surf_xforms, register_scene and the preview trace itself once init has happened.
    if (!polyscope::isInitialized()) return;

    polyscope::removeAllStructures();
    init_surf_xforms();
    register_scene();
    start_trace(10'000);
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

// ---- start_trace / poll_trace / cancel_and_join_trace ------------------------

void Viewer::start_trace(std::size_t n_rays) {
    if (!scene_) return;
    auto* recv = scene_->receiver();
    if (!recv) return;
    // One trace at a time. The buttons are greyed out while one runs, but a stale callback
    // or a scripted call must not be able to launch a second worker over the first.
    if (trace_running_.load(std::memory_order_acquire)) return;
    if (trace_thread_.joinable()) trace_thread_.join();

    // The BVH rebuild is a scene mutation, so it happens here on the GUI thread, before the
    // worker exists — never concurrently with it.
    if (need_rebuild_) {
        scene_->build_acceleration_structure();
        need_rebuild_ = false;
    }

    cfg_.n_primary_rays = n_rays;

    // Everything the worker needs is snapshotted now: the config by value, the accumulator
    // freshly allocated and moved in. After launch the worker reads no Viewer member except
    // trace_ctl_ (atomics) and writes only pending_*.
    const tracer::TraceConfig cfg   = cfg_;
    const bool                multi = recv->is_multi_face();
    const auto&               ra    = recv->accumulator();
    auto work_acc = std::make_unique<tracer::FluxAccumulator>(
        ra.half_width(), ra.half_height(), ra.nx(), ra.ny());

    pending_result_ = {};
    pending_acc_.reset();
    trace_ctl_.reset(n_rays);
    trace_done_.store(false, std::memory_order_relaxed);
    trace_running_.store(true, std::memory_order_release);

    scene::Scene* sc = scene_;
    trace_thread_ = std::jthread(
        [this, sc, cfg, multi, acc = std::move(work_acc)]() mutable {
            tracer::Tracer tr(*sc);
            tracer::TraceResult r = multi ? tr.run(cfg, &trace_ctl_)
                                          : tr.run(cfg, *acc, &trace_ctl_);
            // The multi-face run deposits into the scene receiver's own faces; copy the
            // summary grid out so the GUI never reads live scene state.
            if (multi && !r.cancelled && sc->receiver())
                acc = std::make_unique<tracer::FluxAccumulator>(sc->receiver()->accumulator());

            pending_result_ = std::move(r);
            pending_acc_    = std::move(acc);
            // Release store: every write above happens-before the GUI's acquire load below.
            trace_done_.store(true, std::memory_order_release);
        });
}

void Viewer::poll_trace() {
    if (!trace_done_.load(std::memory_order_acquire)) return;

    // Join before touching pending_*: the join is a second, unconditional happens-before
    // edge, so the GUI cannot observe a half-written result even if the flag were reordered.
    if (trace_thread_.joinable()) trace_thread_.join();
    trace_done_.store(false, std::memory_order_relaxed);
    trace_running_.store(false, std::memory_order_release);

    // A cancelled run holds a partial sum, so it is dropped and the previous result stays on
    // screen; only a completed run is published.
    if (!pending_result_.cancelled) {
        result_ = std::move(pending_result_);
        if (pending_acc_) acc_ = std::move(pending_acc_);
        traced_       = true;
        need_retrace_ = false;

        update_receiver_flux();

        if (!result_.sampled_paths.empty()) {
            RayRenderer renderer(scene_);
            renderer.register_paths(result_);
        }
    }

    pending_result_ = {};
    pending_acc_.reset();
}

void Viewer::cancel_and_join_trace() {
    if (trace_thread_.joinable()) {
        trace_ctl_.request_cancel();
        trace_thread_.join();
    }
    trace_running_.store(false, std::memory_order_release);
    trace_done_.store(false, std::memory_order_relaxed);
    pending_result_ = {};
    pending_acc_.reset();
}

// ---- register_scene ----------------------------------------------------------

void Viewer::register_scene() {
    // Let Polyscope size the scene while the structures land...
    polyscope::options::automaticallyComputeSceneExtents = true;

    RayRenderer renderer(scene_);
    renderer.register_surfaces(32);
    renderer.register_aperture();
    update_receiver_flux(); // registers zeroed heatmap mesh

    // ...then freeze it. Structure::setTransform recomputes the GLOBAL lengthScale and
    // bounding box across every structure, and the default TileReflection ground plane
    // draws a mirrored copy of every object about a plane whose height comes from that box.
    // So dragging one object's scale moved the ground plane and therefore every object's
    // reflection - which read as "everything flashes and scales together". Translate and
    // rotate use the identical path; they just never perturb the box enough to notice.
    polyscope::options::automaticallyComputeSceneExtents = false;
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
    ctx.run_trace             = [this](std::size_t n) { start_trace(n); };

    // Trace progress is read from the control's atomics, so the panel can poll it every
    // frame while the worker is mid-run.
    ctx.trace_running        = trace_running_.load(std::memory_order_acquire);
    ctx.trace_rays_done      = ctx.trace_running ? trace_ctl_.rays_done()  : 0;
    ctx.trace_rays_total     = ctx.trace_running ? trace_ctl_.rays_total() : 0;
    ctx.cancel_trace         = [this]() { trace_ctl_.request_cancel(); };

    // Import needs the scene's own directory so mesh paths stay relative to the file,
    // and a SceneEditor so a committed element reaches both document and live scene.
    ctx.scene_dir             = &scene_dir_;
    if (editor_) {
        ctx.commit_transform = [this](std::uint64_t id, const math::mat4& world) {
            editor_->commit_transform(id, world);
        };
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
    // Pick up a finished worker before anything reads result_ or acc_ this frame.
    poll_trace();

    ImGui::SetNextWindowSize(ImVec2(300, 720), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::Begin("Solar Cooker RT");

    PanelContext ctx = make_panel_context();
    const bool   busy = ctx.trace_running;

    // The worker reads the scene without a lock, so for as long as it runs the scene must be
    // immutable. Every control that can mutate it is greyed out here rather than in each
    // panel: one gate, and no panel can forget it. ImGuizmo is not an ImGui widget and
    // ignores BeginDisabled, so it gets its own switch.
    ImGuizmo::Enable(!busy);

    draw_outliner_panel(ctx);   // selection and visibility only: no scene mutation

    ImGui::BeginDisabled(busy);
    draw_scene_browser_panel(ctx);
    draw_transform_panel(ctx);
    draw_materials_panel(ctx);
    draw_sun_panel(ctx);
    ImGui::EndDisabled();

    draw_trace_panel(ctx);      // owns the Cancel button, so it must stay live

    ImGui::BeginDisabled(busy);
    draw_import_panel(ctx);
    ImGui::EndDisabled();

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

    // set_loaded_scene() may already have started a preview; this one supersedes it.
    cancel_and_join_trace();

    init_surf_xforms();
    register_scene();

    // Quick preview. It goes through the same worker as every other trace: the window opens
    // immediately and the preview lands on a later frame, which matters because a heavy mesh
    // scene's 10k rays are not always cheap, and because one launch path is one race to
    // reason about instead of two.
    cfg_.record_paths        = true;
    cfg_.max_paths_to_record = 200;
    start_trace(10'000);

    polyscope::state::userCallback = [this]() { draw_gui(); };
    polyscope::show();

    // The window is gone; the worker must not outlive it.
    cancel_and_join_trace();

    ImPlot::DestroyContext();
}

} // namespace scrt::viz
