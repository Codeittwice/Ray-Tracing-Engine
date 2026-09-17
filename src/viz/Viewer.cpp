#include "scrt/viz/Viewer.hpp"
#include "scrt/viz/FluxPlotter.hpp"
#include "scrt/viz/Fonts.hpp"
#include "scrt/viz/Icons.hpp"
#include "scrt/viz/Panels.hpp"
#include "scrt/viz/RayRenderer.hpp"
#include "scrt/viz/Layout.hpp"
#include "scrt/viz/Theme.hpp"
#include "scrt/viz/ViewSettings.hpp"
#include "scrt/core/Transform.hpp"
#include "scrt/io/SceneLoader.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/scene/Receiver.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"       // must precede ImGuizmo.h: ImGuizmo.h declares against ImGui types
#include "ImGuizmo.h"    // NOLINT(build/include_order)
#include "implot.h"
#include "polyscope/polyscope.h"
#include "polyscope/imgui_config.h"
#include <GLFW/glfw3.h>
#include "polyscope/surface_mesh.h"

namespace scrt::viz {

/// Rays for the automatic preview that runs on load.
///
/// 10k over a 64x64 receiver is ~2 rays per bin, which renders as pure Poisson confetti rather
/// than a flux map. Note the trade-off runs the other way from intuition: a FINER receiver grid
/// makes the noise worse, because each bin catches fewer rays. Smoothing it means more rays.
/// The trace is asynchronous, so this costs responsiveness rather than startup time.
static constexpr std::size_t kPreviewRays = 150'000;


namespace {

void register_receiver_face_flux(const scene::ReceiverFace& face,
                                 const tracer::FluxAccumulator& acc,
                                 const std::string& mesh_name,
                                 double source_power_w) {
    const int nx = acc.nx(), ny = acc.ny();
    const double hw = acc.half_width(), hh = acc.half_height();
    const auto& xf = face.surface()->transform();
    // Display only: the same blur the analysis plot uses, so the receiver in the 3D view and the
    // heatmap beside it show one result rather than two. Every reported and exported number
    // still comes from the accumulator's raw bins.
    const std::vector<double> smoothed =
        gaussian_smooth(acc.flux_map_wm2(), nx, ny,
                        static_cast<double>(flux_smoothing_sigma()));
    const auto& flux = smoothed;

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
    q->setColorMap(flux_colormap());
    // The same FIXED scale as the heatmap beside it: 100% = source power / receiver area, over the
    // sensitivity. Polyscope clamps above the range in its shader, so no value needs clamping here.
    const double reference = flux_reference_wm2(source_power_w, hw, hh);
    if (reference > 0.0)
        q->setMapRange({0.0, reference / static_cast<double>(flux_sensitivity())});
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

void Viewer::set_loaded_scene(io::LoadedScene ls, std::filesystem::path scene_file) {
    // Takes the scene FILE, not its directory: the save panel needs a path to write back to,
    // and the mesh-path rebase needs the directory. Deriving one from the other here keeps
    // the two from drifting apart.
    scene_path_ = std::move(scene_file);
    scene_dir_  = scene_path_.empty() ? std::filesystem::path{} : scene_path_.parent_path();
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

    // The QA scenes used to check each feature live one folder down, OUTSIDE the four folders the
    // regression corpus sweeps, and are listed after the examples. Without this they could only be
    // opened through the file dialog, and the user moved one up into examples/ to reach it - which
    // silently added it to the corpus.
    const auto qa_dir = examples_dir_ / "feature_checks";
    std::vector<std::filesystem::path> qa;
    if (std::filesystem::is_directory(qa_dir))
        for (const auto& e : std::filesystem::directory_iterator(qa_dir))
            if (e.path().extension() == ".json") qa.push_back(e.path());
    std::sort(qa.begin(), qa.end());

    for (const auto& p : available_scenes_)
        scene_display_names_.push_back(p.filename().string());
    for (const auto& p : qa) {
        available_scenes_.push_back(p);
        scene_display_names_.push_back("QA / " + p.filename().string());
    }
}

// ---- load_from_file ----------------------------------------------------------

void Viewer::load_from_file(const std::filesystem::path& path) {
    auto ls = io::load_scene(path);
    // Remember where it came from: Save writes here and Revert reloads from here.
    scene_path_ = path;
    scene_dir_  = path.parent_path();
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
    start_trace(kPreviewRays);
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

// ---- element lifecycle -------------------------------------------------------

void Viewer::adopt_element(std::uint64_t id) {
    if (!scene_ || !polyscope::isInitialized()) return;

    // reregister_surface both registers a surface that has none and replaces one whose geometry
    // changed, so it is the single entry point for "make the view match this element".
    RayRenderer(scene_, editor_ ? &editor_->doc() : nullptr).reregister_surface(id, 32);

    if (const auto* s = scene_->surface_by_id(id)) {
        // A fresh base pose: the element is exactly where its document says it is, and no gizmo
        // delta has been applied to it yet.
        ObjectEditState st;
        st.base    = s->transform();
        edits_[id] = st;
    }
    need_rebuild_ = true;
    need_retrace_ = true;
}

void Viewer::release_element(std::uint64_t id) {
    if (polyscope::isInitialized()) RayRenderer(scene_).remove_surface_structure(id);
    edits_.erase(id);
    if (selected_id_ == id) selected_id_ = 0;
    need_rebuild_ = true;
    need_retrace_ = true;
}

void Viewer::apply_element_ops() {
    if (pending_element_ops_.empty()) return;

    // Drained by move before anything is applied: an op that throws must not leave the queue
    // half-consumed and ready to re-run its first half on the next frame.
    const auto ops = std::move(pending_element_ops_);
    pending_element_ops_.clear();

    if (!editor_) return;

    for (const auto& op : ops) {
        try {
            switch (op.kind) {
                case ElementOp::Kind::Add: {
                    const std::uint64_t id = editor_->add_element(op.doc);
                    if (id != 0) adopt_element(id);
                    break;
                }
                case ElementOp::Kind::Duplicate: {
                    const std::uint64_t id = editor_->duplicate_element(op.id);
                    if (id != 0) adopt_element(id);
                    break;
                }
                case ElementOp::Kind::AddMaterial:
                    // Nothing to adopt: a material is not drawn on its own, only through the
                    // surfaces that borrow it.
                    editor_->add_material(op.material);
                    break;
                case ElementOp::Kind::AddSource:
                    // Applied in queue order, so a source added after elements in the same
                    // frame sees them: a sun asking for an auto-fitted aperture needs that.
                    editor_->add_source(op.source);
                    need_rebuild_ = true;
                    need_retrace_ = true;
                    break;
                case ElementOp::Kind::Remove:
                    // View first, then the thing itself. The structure registry is keyed by id
                    // rather than by pointer so the order is not load-bearing, but dropping the
                    // picture of an object before destroying the object is the readable order.
                    release_element(op.id);
                    editor_->remove_element(op.id);
                    break;
            }
        } catch (const std::exception& e) {
            // add_element throws on an unknown material id or an unreadable mesh, and documents
            // that it leaves both the scene and the document untouched when it does.
            load_error_ = e.what();
        }
    }
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
    // worker exists â€” never concurrently with it.
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
            tracer::Tracer      tr(*sc);
            tracer::TraceResult r;
            // A throw escaping a jthread is std::terminate. The tracer refuses a coherent receiver
            // with a randomly sampled source (a laser added from the library after load, say), and
            // that refusal must reach the user as a message, not take the app down.
            try {
                r = multi ? tr.run(cfg, &trace_ctl_) : tr.run(cfg, *acc, &trace_ctl_);
            } catch (const std::exception& e) {
                r.cancelled    = true;
                pending_error_ = e.what();
            }
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

    trace_error_ = std::move(pending_error_);
    pending_error_.clear();

    // A cancelled run holds a partial sum, so it is dropped and the previous result stays on
    // screen; only a completed run is published.
    if (!pending_result_.cancelled) {
        result_ = std::move(pending_result_);
        if (pending_acc_) acc_ = std::move(pending_acc_);
        traced_       = true;
        need_retrace_ = false;

        update_receiver_flux();

        if (!result_.sampled_paths.empty()) {
            RayRenderer renderer(scene_, editor_ ? &editor_->doc() : nullptr);
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
    // Scene extents stay automatic here, so registering (and a runtime model import) resizes
    // the ground plane and the lengthScale-relative sizes to fit what is actually on screen.
    // They are frozen only for the duration of a transform drag, by the transform panel - a
    // session-long freeze would leave an imported model outside the scene's idea of its own
    // size. See set_extents_frozen() in TransformPanel.cpp.
    RayRenderer renderer(scene_, editor_ ? &editor_->doc() : nullptr);
    renderer.register_surfaces(32);
    renderer.register_aperture();
    renderer.register_sources();
    update_receiver_flux(); // registers zeroed heatmap mesh

    // Tell the flux window whether a concentration ratio even exists for this scene. It is
    // peak flux over the sun's DNI, and a laser bench has no DNI to divide by; without this
    // the window would print a ratio against an assumed 1000 W/m2 for a 5 mW laser.
    if (scene_) {
        if (const auto* sun = scene_->primary_sun()) FluxPlotter::set_scene_dni(sun->dni());
        else                                          FluxPlotter::set_no_sun();
    }

    // Pin the ground plane to a fixed height taken from the scene we just loaded.
    //
    // In Automatic mode the floor sits a fixed offset under the scene's bounding box, so it
    // tracks that box - and the box is recomputed from EVERY structure whenever any one of them
    // is transformed. Scaling a single reflector therefore moved the floor, which is the
    // "background changes while scaling" report. Manual mode reads options::groundPlaneHeight
    // and ignores the bounding box entirely, so the floor stays put no matter what is edited.
    if (scene_) {
        const auto b = scene_->world_bounds();
        const double span = b.max().z - b.min().z;
        polyscope::options::groundPlaneHeightMode = polyscope::GroundPlaneHeightMode::Manual;
        polyscope::options::groundPlaneHeight =
            static_cast<float>(b.min().z - 0.02 * (span > 0.0 ? span : 1.0));
    }
}

// ---- update_receiver_flux ----------------------------------------------------

void Viewer::update_receiver_flux() {
    if (!scene_ || !scene_->receiver()) return;

    // Every source's power, published for the flux window too: both views scale against it.
    double source_power = 0.0;
    for (const auto& src : scene_->sources()) source_power += src->total_power_w();
    FluxPlotter::set_scene_source_power(source_power);

    auto* recv = scene_->receiver();
    if (recv->is_multi_face()) {
        for (const auto& face : recv->faces()) {
            register_receiver_face_flux(
                *face, face->accumulator(), "receiver_" + face->name() + "_flux", source_power);
        }
        return;
    }

    if (!acc_) return;
    register_receiver_face_flux(*recv->faces().front(), *acc_, "receiver_flux", source_power);
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

    // Deferred, NOT immediate. load_from_file() destroys the Scene and the SceneEditor, and
    // PanelContext hands every panel raw pointers to both - so loading from inside a panel left
    // every panel drawn after it in that same frame reading freed memory. Crashed on any scene
    // change from the browser.
    ctx.load_scene           = [this](const std::filesystem::path& path) { pending_load_ = path; };
    ctx.run_trace             = [this](std::size_t n) { start_trace(n); };

    // Trace progress is read from the control's atomics, so the panel can poll it every
    // frame while the worker is mid-run.
    ctx.trace_running        = trace_running_.load(std::memory_order_acquire);
    ctx.trace_error          = &trace_error_;
    ctx.trace_rays_done      = ctx.trace_running ? trace_ctl_.rays_done()  : 0;
    ctx.trace_rays_total     = ctx.trace_running ? trace_ctl_.rays_total() : 0;
    ctx.cancel_trace         = [this]() { trace_ctl_.request_cancel(); };

    // Import needs the scene's own directory so mesh paths stay relative to the file,
    // and a SceneEditor so a committed element reaches both document and live scene.
    ctx.scene_dir             = &scene_dir_;
    // Without these the save panel has no document and reports "No document to save".
    ctx.settings_open   = &settings_open_;
    ctx.open_settings   = [this]() { settings_open_ = !settings_open_; };
    ctx.io_overlay_open = &io_overlay_open_;
    ctx.ai_overlay_open = &ai_overlay_open_;
    ctx.focus_tab       = [this](const char* tab) { focus_tab_ = tab; };
    ctx.editor         = editor_.get();
    ctx.scene_path     = &scene_path_;
    ctx.set_scene_path = [this](const std::filesystem::path& p) { scene_path_ = p; };

    if (editor_) {
        ctx.commit_transform = [this](std::uint64_t id, const math::mat4& world) {
            editor_->commit_transform(id, world);
        };
        // All three are QUEUED, not applied â€” see pending_element_ops_. They return only whether
        // the request was accepted, because the element does not exist yet when they return.
        ctx.add_element = [this](io::ElementDoc d) {
            pending_element_ops_.push_back({ElementOp::Kind::Add, 0, std::move(d), {}, {}});
            return true;
        };
        ctx.remove_element = [this](std::uint64_t id) {
            pending_element_ops_.push_back({ElementOp::Kind::Remove, id, {}, {}, {}});
        };
        ctx.duplicate_element = [this](std::uint64_t id) {
            pending_element_ops_.push_back({ElementOp::Kind::Duplicate, id, {}, {}, {}});
        };
        ctx.add_material = [this](io::MaterialDoc m) {
            // Refused here rather than at apply time so the panel can report it on the click.
            // A duplicate id is the common case - the same component dropped twice - and is not
            // an error, but the caller still needs to know nothing was added.
            if (m.id.empty()) return false;
            if (editor_) {
                for (const auto& d : editor_->doc().materials)
                    if (d.id == m.id) return false;
            }
            pending_element_ops_.push_back(
                {ElementOp::Kind::AddMaterial, 0, {}, std::move(m), {}});
            return true;
        };
        ctx.add_source = [this](io::SourceDoc s) {
            pending_element_ops_.push_back(
                {ElementOp::Kind::AddSource, 0, {}, {}, std::move(s)});
            return true;
        };
    }

    return ctx;
}

// ---- draw_gui ----------------------------------------------------------------

void Viewer::draw_gui() {
    // Fill the screen on launch. Done from the first drawn frame rather than from run(), where
    // Polyscope's GLFW window is still hidden (GLFW_VISIBLE=false until show() runs) and a
    // maximise request has no window on screen to act on.
    if (!maximized_) {
        maximized_ = true;
        if (GLFWwindow* win = glfwGetCurrentContext()) glfwMaximizeWindow(win);
    }

    // Pick up a finished worker before anything reads result_ or acc_ this frame.
    poll_trace();

    PanelContext ctx  = make_panel_context();
    const bool   busy = ctx.trace_running;

    // The menu bar claims the top strip, so the panels start below whatever height it reports.
    // Recomputed every frame from the live window size with ImGuiCond_Always: the old
    // FirstUseEver positioned each window once and never again, so the panels froze wherever
    // they were last left and ignored a resize.
    // A little more vertical room than ImGui's default menu bar, so the icon controls on the
    // right have somewhere to sit.
    const float k = ui_scale();
    // Before anything draws: a click in the viewport must reach the selection whichever tab is
    // open, and the outliner - which used to own this - only runs on one of the three.
    update_selection_from_view(ctx);

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(9.0f * k, 9.0f * k));
    const float bar = draw_top_bar(ctx);
    ImGui::PopStyleVar();

    const LayoutRects lay =
        compute_layout(ImGui::GetIO().DisplaySize, bar, selection_height_, k);

    // ImGuizmo must be primed every frame, before anything can manipulate. It is primed inside
    // draw_transform_panel, which the selection window calls - and that window draws after the
    // left column. Nothing in the left column manipulates, so the order holds.

    // ---- left column: one window, three tabs --
    //
    // Tabs rather than the previous stack of nine collapsing headers. The stack meant the
    // controls a user wanted were usually below the fold, and which ones were open was
    // remembered from whatever they last did rather than from what they are doing now.
    ImGui::SetNextWindowPos(lay.left_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(lay.left_size, ImGuiCond_Always);
    ImGui::Begin("Solar Cooker RT", nullptr, docked_panel_flags());

    // The worker reads the scene without a lock, so for as long as it runs the scene must be
    // immutable. Every control that can mutate it is greyed out here rather than in each
    // panel: one gate, and no panel can forget it. ImGuizmo is not an ImGui widget and
    // ignores BeginDisabled, so it gets its own switch.
    ImGuizmo::Enable(!busy);

    if (ImGui::BeginTabBar("##workspace", ImGuiTabBarFlags_None)) {
        // A tab the import/export overlay asked for, honoured once. SetSelected has to be
        // supplied to BeginTabItem on the frame the switch happens, not set afterwards.
        const auto wants = [this](const char* name) {
            return (focus_tab_ && std::strcmp(focus_tab_, name) == 0)
                       ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None;
        };

        if (ImGui::BeginTabItem(ICON_FA_LAYER_GROUP " Scene", nullptr, wants("Scene"))) {
            draw_outliner_panel(ctx, /*boxed=*/false);   // selection only: no scene mutation
            ImGui::Spacing();
            ImGui::BeginDisabled(busy);
            draw_scene_browser_panel(ctx);
            draw_save_panel(ctx);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_CUBE " Library", nullptr, wants("Library"))) {
            ImGui::BeginDisabled(busy);
            draw_library_panel(ctx);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_PALETTE " Design", nullptr, wants("Design"))) {
            ImGui::BeginDisabled(busy);
            draw_materials_panel(ctx);
            draw_import_panel(ctx);
            ImGui::EndDisabled();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem(ICON_FA_PLAY " Simulate", nullptr, wants("Simulate"))) {
            ImGui::BeginDisabled(busy);
            draw_sun_panel(ctx);
            ImGui::EndDisabled();
            draw_trace_panel(ctx);   // owns the Cancel button, so it must stay live
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        focus_tab_ = nullptr;   // one-shot; the user owns the tab again from here
    }
    ImGui::End();

    // A component dragged out of the library needs somewhere to land, and the 3D view is not
    // an ImGui window. This creates its overlay only while a drag is in flight.
    draw_viewport_drop_target(ctx, lay.viewport_min, lay.viewport_max);
    draw_trace_toolbar(ctx, lay.viewport_min, lay.viewport_max);

    // ---- right column, bottom: what is selected, and how to place it --
    ImGui::SetNextWindowPos(lay.right_bottom_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(lay.right_bottom_size, ImGuiCond_Always);
    ImGui::Begin("Selection", nullptr, docked_panel_flags());
    ImGui::BeginDisabled(busy);
    const float wanted = draw_selection_panel(ctx);
    ImGui::EndDisabled();
    ImGui::End();

    // Ease toward the requested height rather than snapping to it, so selecting an object grows
    // the panel instead of making the flux plot above it jump.
    selection_height_ += (wanted - selection_height_) * 0.25f;
    if (std::abs(wanted - selection_height_) < 0.5f) selection_height_ = wanted;

    // ---- right column, top: flux analysis --
    ImGui::SetNextWindowPos(lay.right_top_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(lay.right_top_size, ImGuiCond_Always);
    if (traced_ && acc_) {
        static FluxPlotter plotter;
        plotter.draw(*acc_, result_);
    } else {
        // The window exists even before the first trace lands, so the column does not visibly
        // assemble itself a second after launch.
        ImGui::Begin("Flux Analysis", nullptr, docked_panel_flags());
        ImGui::TextDisabled("No results yet");
        ImGui::Spacing();
        ImGui::TextWrapped("Run a trace from the Simulate tab to see how much sunlight reaches "
                           "the pot and where it lands.");
        ImGui::End();
    }

    draw_settings_window(ctx);
    draw_io_overlay(ctx);
    draw_ai_overlay(ctx);
    draw_viewport_buttons(ctx, lay.viewport_min, lay.viewport_max);

    // Deferred mutations, now that no panel holds a pointer or a span into the scene.
    //
    // A pending load replaces the whole scene, so element ops queued against the outgoing one are
    // dropped rather than applied to something that is about to be destroyed.
    if (pending_load_) {
        pending_element_ops_.clear();
        const auto path = *pending_load_;
        pending_load_.reset();
        load_from_file(path);
        return;
    }
    apply_element_ops();
    // Every frame, but it does work only when the setting or the step changed, or a scene load
    // removed every structure (and with them the grid).
    sync_grid(scene_);
    sync_axis(scene_);
    // The flux-map sensitivity slider lives in the flux window; the 3D receiver follows it here.
    static float drawn_sensitivity = flux_sensitivity();
    if (flux_sensitivity() != drawn_sensitivity) {
        drawn_sensitivity = flux_sensitivity();
        update_receiver_flux();
    }
}


// ---- run ---------------------------------------------------------------------

void Viewer::run() {
    if (!scene_) return;

    polyscope::options::programName = "Solar Cooker Ray Tracer";
    polyscope::options::verbosity   = 0;

    // Polyscope's own "Polyscope" / "Structures" / "Selection" windows are placed at fixed
    // spots that collide with ours - Structures lands on top of the left panel, Selection on
    // top of Flux Analysis. We own the layout instead; SettingsPanel can bring individual
    // ones back once we know which built-ins are actually worth keeping.
    polyscope::options::buildDefaultGuiPanels = show_polyscope_panels_;

    // Polyscope otherwise wraps our callback in its own Begin("##Command UI"). We open our
    // own windows inside it, leaving a visible empty ~500x42 shell in the top-right corner.
    polyscope::options::openImGuiWindowForUserCallback = false;

    // .polyscope.ini restores a previous window geometry over anything we set, and is written
    // to the current working directory on exit - so the app's startup size depended on where
    // it was launched from.
    polyscope::options::usePrefsFile = false;

    // show() creates a BRAND NEW ImGuiContext and re-runs this callback, so it is the only
    // hook that lands on every context and early enough to stop imgui.ini being read at all.
    // Mutating ImGui::GetStyle() between init() and show() is silently discarded.
    polyscope::options::configureImGuiStyleCallback = []() {
        ImGuiIO& io   = ImGui::GetIO();
        io.IniFilename = nullptr;                       // no saved window positions, ever
        // A plain click on a numeric field types into it; a drag still drags. The default
        // wants Ctrl+click, which nobody discovers, so the numeric fields read as drag-only.
        io.ConfigDragClickToInputText = true;
        // Our own palette, not Polyscope's green/teal. Re-applied here rather than once after
        // init() because show() builds a fresh ImGuiContext with a fresh ImGuiStyle.
        apply_theme(current_theme());
    };

    // Adds Font Awesome to Polyscope's Lato. Unlike the style callback this runs exactly once,
    // when the render engine builds its shared atlas - the atlas survives the per-show() context
    // churn, so the ImFont pointers it hands back stay valid.
    polyscope::options::prepareImGuiFontsCallback = []() { return prepare_fonts(); };

    polyscope::init();
    ImPlot::CreateContext();

    // Maximising happens on the first drawn frame instead (see draw_gui). Polyscope creates
    // its GLFW window hidden and only shows it from inside show(); a maximise request against a
    // hidden window is dropped, which is why the app came up at 1302x776 with this call in
    // place and every option below correctly set.

    // This project is Z-up throughout: +Z is the zenith in the sun model, and the box receiver
    // walls are named north/south/east/west in the XY plane. Polyscope defaults to Y-up, which
    // drew the ground plane as a tilted wall standing through the cooker rather than a floor
    // under it.
    polyscope::view::setUpDir(polyscope::UpDir::ZUp);

    // Off by default: it is a depth cue, not optics, and it dominates the view on a cooker that
    // sits close to the ground. Settings turns it back on.
    polyscope::options::groundPlaneMode = polyscope::GroundPlaneMode::None;

    // The 3D area is most of the screen; leaving it near-white under dark panels reads as two
    // applications sharing a window.
    polyscope::view::bgColor = viewport_background(current_theme());

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
    start_trace(kPreviewRays);

    polyscope::state::userCallback = [this]() { draw_gui(); };
    polyscope::show();

    // The window is gone; the worker must not outlive it.
    cancel_and_join_trace();

    ImPlot::DestroyContext();
}

} // namespace scrt::viz
