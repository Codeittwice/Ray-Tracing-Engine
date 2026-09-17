#include "scrt/tracer/Tracer.hpp"
#include "scrt/tracer/EmissionPlan.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/sources/LightSource.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <algorithm>
#include <chrono>
#include <execution>
#include <limits>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace scrt::tracer {

namespace {

/// Folds one interaction into a ray's branch tag: which surface, and whether it reflected (0) or went
/// through (1). Two rays with equal tags travelled the same physical arm, which is what lets a
/// coherent receiver normalise each arm by its own ray count before adding arms as fields.
std::uint32_t mix_branch(std::uint32_t branch, std::uint64_t surface_id, unsigned how) {
    std::uint64_t x = (static_cast<std::uint64_t>(branch) << 32) ^ (surface_id * 2u + how);
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return static_cast<std::uint32_t>(x);
}

/// Tags both outgoing branches of an interaction. Touches only Ray::branch: no power, direction or
/// draw, so every existing result is unchanged.
void stamp_branches(materials::Interaction& ia, const surfaces::Surface& s) {
    ia.reflected.branch   = mix_branch(ia.reflected.branch, s.id(), 0u);
    ia.transmitted.branch = mix_branch(ia.transmitted.branch, s.id(), 1u);
}

/// Index of src in the scene's source list (0 if absent, which cannot happen for a planned source).
std::uint32_t source_index(const scene::Scene& sc, const sources::LightSource* src) {
    const auto list = sc.sources();
    for (std::size_t i = 0; i < list.size(); ++i)
        if (list[i].get() == src) return static_cast<std::uint32_t>(i);
    return 0u;
}

/// Coherence length per source index for a coherent receiver; THROWS if any source samples at
/// random. build_scene already refuses such a file - this guards a source added in the editor since.
std::vector<double> coherence_lengths_or_throw(const scene::Scene& sc) {
    std::vector<double> out;
    for (const auto& src : sc.sources()) {
        if (!src->deterministic_sampling())
            throw std::runtime_error("Tracer: a coherent receiver needs every source to use grid "
                                     "sampling; a '" + std::string(src->type_name()) +
                                     "' source samples at random, which would give speckle.");
        out.push_back(src->coherence_length_m());
    }
    for (const auto& m : sc.materials())
        if (m && !m->deterministic())
            throw std::runtime_error("Tracer: a coherent receiver cannot sum rays from a material that scatters "
                                     "at random (a diffuser, or a real mirror with slope error): its "
                                     "rays would add as speckle.");
    return out;
}

} // namespace

void Tracer::trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                       RayPath* path, std::uint32_t parent_node, int max_bounces,
                       double power_cutoff, std::size_t& hit_count) const {
    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, scrt::math::EPSILON_T,
                               std::numeric_limits<double>::max(), h))
            return;

        // Optical path length: the distance just travelled times the index of the medium it was
        // travelled in. Pure bookkeeping for the coherent receiver (Wave 5); it touches no power,
        // direction or RNG draw, so every result is unchanged. h.t is a true distance because
        // directions are unit vectors and every material starts its outgoing ray AT the hit.
        r.opl_m += r.medium_n * h.t;

        if (path) {
            // One node per hit, one edge from wherever this ray came from. Recording the edge's
            // power here (before the material acts) is what lets the renderer show how much
            // light each branch of a split actually carries.
            const auto node = static_cast<std::uint32_t>(path->nodes.size());
            path->nodes.push_back(h.position);
            path->edges.push_back({parent_node, node});
            path->edge_power_w.push_back(r.power);
            path->edge_info.push_back({r.wavelength_nm, r.power, r.opl_m, r.polarised, r.direction,
                                       r.s_axis, r.Es, r.Ep});
            parent_node = node;
        }

        ++hit_count;

        // Deposit only on the receiver's own faces, as the receiver overload below does. This
        // overload used to deposit on ANY absorbed hit, binned by that surface's own local
        // coordinates, so a black iris or a beam dump anywhere in the scene was booked as
        // light on the screen. No shipped scene binds an absorber to an element, which is why
        // the corpus never showed it and why this change moves no corpus result. Every
        // receiver face is treated as record-and-stop here, as it always was.
        const int face_index = scene_->receiver()
            ? scene_->receiver()->face_index_for_surface(h.surface)
            : -1;
        if (face_index >= 0) {
            acc.deposit(r, h);
            return;
        }

        auto inter = h.surface->material()->interact(r, h, rng);
        stamp_branches(inter, *h.surface);
        switch (inter.kind) {
            case materials::InteractionKind::Absorbed:
                return;
            case materials::InteractionKind::Reflected:
                r = inter.reflected;
                break;
            case materials::InteractionKind::Refracted:
                r = inter.transmitted;
                break;
            case materials::InteractionKind::Split:
                // `max_bounces - b - 1` is the budget this ray has LEFT, not a fresh one. The
                // reflected branch used to restart at max_bounces, so total path length was
                // bounded only by the power cutoff and a cascade of splitters could branch
                // without limit. Measured before changing it: on the only golden scene that
                // splits, the deepest total path is 2 against a limit of 8, so no result moves.
                //
                // Still a recursive call, deliberately. Rewriting this as an explicit stack
                // would reorder the RNG draws - today the whole reflected sub-tree is traced
                // before the transmitted ray continues - and every dielectric scene would shift.
                if (inter.reflected.power >= power_cutoff)
                    trace_one(inter.reflected, acc, rng, path, parent_node, max_bounces - b - 1,
                              power_cutoff, hit_count);
                r = inter.transmitted;
                break;
        }
        if (r.power < power_cutoff)
            return;
    }
}

void Tracer::trace_one(core::Ray r, scene::Receiver& receiver, math::Rng& rng,
                       RayPath* path, std::uint32_t parent_node, int max_bounces,
                       double power_cutoff, std::size_t& hit_count) const {
    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, scrt::math::EPSILON_T,
                               std::numeric_limits<double>::max(), h))
            return;

        // Optical path length: the distance just travelled times the index of the medium it was
        // travelled in. Pure bookkeeping for the coherent receiver (Wave 5); it touches no power,
        // direction or RNG draw, so every result is unchanged. h.t is a true distance because
        // directions are unit vectors and every material starts its outgoing ray AT the hit.
        r.opl_m += r.medium_n * h.t;

        if (path) {
            // One node per hit, one edge from wherever this ray came from. Recording the edge's
            // power here (before the material acts) is what lets the renderer show how much
            // light each branch of a split actually carries.
            const auto node = static_cast<std::uint32_t>(path->nodes.size());
            path->nodes.push_back(h.position);
            path->edges.push_back({parent_node, node});
            path->edge_power_w.push_back(r.power);
            path->edge_info.push_back({r.wavelength_nm, r.power, r.opl_m, r.polarised, r.direction,
                                       r.s_axis, r.Es, r.Ep});
            parent_node = node;
        }

        ++hit_count;

        const int face_index = scene_->receiver()
            ? scene_->receiver()->face_index_for_surface(h.surface)
            : -1;
        if (face_index >= 0) {
            auto& face = *receiver.mutable_faces()[static_cast<std::size_t>(face_index)];
            face.accumulator().deposit(r, h);
            if (face.mode() == scene::ReceiverFaceMode::RecordAbsorb)
                return;

            r.origin = h.position + r.direction * scrt::math::EPSILON_T;
            // The nudge past a pass-through receiver face is 1 um - more than a wavelength - so it is
            // optical path too; leaving it out would be a phase error of over one full cycle.
            r.opl_m += r.medium_n * scrt::math::EPSILON_T;
            if (r.power < power_cutoff)
                return;
            continue;
        }

        auto inter = h.surface->material()->interact(r, h, rng);
        stamp_branches(inter, *h.surface);
        switch (inter.kind) {
            case materials::InteractionKind::Absorbed:
                return;
            case materials::InteractionKind::Reflected:
                r = inter.reflected;
                break;
            case materials::InteractionKind::Refracted:
                r = inter.transmitted;
                break;
            case materials::InteractionKind::Split:
                if (inter.reflected.power >= power_cutoff)
                    trace_one(inter.reflected, receiver, rng, path, parent_node,
                              max_bounces - b - 1, power_cutoff, hit_count);
                r = inter.transmitted;
                break;
        }
        if (r.power < power_cutoff)
            return;
    }
}

TraceResult Tracer::run(const TraceConfig& cfg, TraceControl* ctl) const {
    auto* scene_receiver = const_cast<scene::Receiver*>(scene_->receiver());
    if (!scene_receiver)
        return {};

    scene_receiver->clear_accumulators();
    if (scene_receiver->coherent())
        scene_receiver->set_coherence_lengths(coherence_lengths_or_throw(*scene_));

    auto t0 = std::chrono::steady_clock::now();

    // Each source states its own strength in watts (for a sun: DNI * aperture area *
    // foreshortening); the plan divides the primary rays among them by power and prices
    // each ray at total_power_w() / count. A scene with nothing emitting traces to nothing,
    // the way a scene with no receiver does.
    const EmissionPlan plan = build_emission_plan(scene_->sources(), cfg.n_primary_rays);
    if (plan.empty()) {
        // Nothing emits: no source, or every source at 0 W (a sun that has set). Zero rays
        // are traced, but the receiver is still finalized so a caller reads zeros, not the
        // state it was in before, and the run is still timed.
        scene_receiver->finalize(cfg.n_primary_rays);
        TraceResult none;
        none.wall_time_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return none;
    }

    int nthreads = cfg.num_threads > 0
                       ? cfg.num_threads
                       : static_cast<int>(std::thread::hardware_concurrency());
    if (nthreads < 1) nthreads = 1;

    std::vector<int> slots(nthreads);
    std::iota(slots.begin(), slots.end(), 0);

    std::size_t base_rays   = cfg.n_primary_rays / static_cast<std::size_t>(nthreads);
    std::size_t extra_rays  = cfg.n_primary_rays % static_cast<std::size_t>(nthreads);

    std::vector<scene::Receiver> slot_receivers;
    slot_receivers.reserve(nthreads);
    for (int i = 0; i < nthreads; ++i)
        slot_receivers.push_back(scene_receiver->clone_empty());

    TraceResult result;
    result.primary_rays_traced = cfg.n_primary_rays;

    // Rays between cancel checks. Hoisted out of the slot loop so the uncontrolled path
    // never touches the control at all.
    const std::size_t check_interval = ctl && ctl->check_interval > 0 ? ctl->check_interval : 4096;
    std::size_t       rays_traced    = 0;

    std::mutex path_mutex;

    std::for_each(std::execution::par, slots.begin(), slots.end(),
                  [&](int slot) {
        std::size_t slot_rays = base_rays + (static_cast<std::size_t>(slot) < extra_rays ? 1 : 0);
        std::size_t ray_start = base_rays * static_cast<std::size_t>(slot) +
                                std::min(static_cast<std::size_t>(slot), extra_rays);

        std::uint64_t seed = cfg.rng_seed != 0
                                 ? cfg.rng_seed + static_cast<std::uint64_t>(slot) * 6364136223846793005ULL
                                 : static_cast<std::uint64_t>(
                                       std::chrono::steady_clock::now()
                                           .time_since_epoch()
                                           .count()) +
                                       static_cast<std::uint64_t>(slot) * 6364136223846793005ULL;

        math::Rng slot_rng(seed);
        scene::Receiver& slot_receiver = slot_receivers[static_cast<std::size_t>(slot)];
        std::size_t hits = 0;

        std::size_t traced           = 0;    ///< Rays this slot actually traced.
        std::size_t since_last_check = 0;    ///< Rays traced since progress was last published.
        bool        check_due        = true; ///< Poll before the first ray, so a pre-cancelled run does nothing.

        for (std::size_t i = 0; i < slot_rays; ++i) {
            if (ctl && check_due) {
                ctl->add_done(since_last_check);
                since_last_check = 0;
                if (ctl->should_stop()) break;
                check_due = false;
            }

            RayPath* path_ptr = nullptr;
            RayPath  path_buf;

            {
                if (cfg.record_paths) {
                    std::lock_guard<std::mutex> lk(path_mutex);
                    if (result.sampled_paths.size() < cfg.max_paths_to_record)
                        path_ptr = &path_buf;
                }
            }

            const EmissionPlan::Entry& e = plan.entry_for(ray_start + i);
            core::Ray r = e.source->sample_ray_indexed(slot_rng, (ray_start + i) - e.first, e.count);
            r.power    *= e.per_ray_w;
            r.id        = static_cast<std::uint32_t>(ray_start + i);
            r.source    = source_index(*scene_, e.source);

            if (path_ptr) {
                path_buf.nodes.push_back(r.origin);   // node 0: where the ray began
                path_buf.monochromatic = (e.source->type_name() == "laser");
            }
            trace_one(r, slot_receiver, slot_rng, path_ptr, 0u, cfg.max_bounces,
                      std::min(cfg.power_cutoff_w, cfg.power_cutoff_rel * r.power), hits);

            if (path_ptr && !path_buf.edges.empty()) {
                std::lock_guard<std::mutex> lk(path_mutex);
                if (result.sampled_paths.size() < cfg.max_paths_to_record)
                    result.sampled_paths.push_back(std::move(path_buf));
            }

            ++traced;
            if (ctl && ++since_last_check >= check_interval) check_due = true;
        }

        if (ctl) ctl->add_done(since_last_check);

        {
            std::lock_guard<std::mutex> lk(path_mutex);
            result.total_hits += hits;
            rays_traced       += traced;
        }
    });

    for (auto& receiver : slot_receivers)
        scene_receiver->merge_from(receiver);

    // Normalisation stays tied to the *requested* count: each ray already carries
    // total_power / n_primary_rays, so a cancelled run is a correctly weighted partial sum,
    // not a rescaled one. With no cancellation rays_traced == cfg.n_primary_rays exactly.
    scene_receiver->finalize(cfg.n_primary_rays);

    result.primary_rays_traced = rays_traced;
    result.cancelled           = ctl && ctl->cancel_requested();

    auto t1 = std::chrono::steady_clock::now();
    result.wall_time_s = std::chrono::duration<double>(t1 - t0).count();

    return result;
}

TraceResult Tracer::run(const TraceConfig& cfg, FluxAccumulator& acc, TraceControl* ctl) const {
    auto t0 = std::chrono::steady_clock::now();

    // The caller's accumulator sums the way the scene's receiver is declared to, so the GUI preview
    // and scrt_compare show interference exactly when the file asks for it.
    const bool coherent = scene_->receiver() && scene_->receiver()->coherent();
    std::vector<double> coherence;
    if (coherent) coherence = coherence_lengths_or_throw(*scene_);
    if (acc.coherent() != coherent) acc.set_coherent(coherent);
    if (coherent) acc.set_coherence_lengths(coherence);

    // Each source states its own strength in watts (for a sun: DNI * aperture area *
    // foreshortening); the plan divides the primary rays among them by power and prices
    // each ray at total_power_w() / count. A scene with nothing emitting traces to nothing,
    // the way a scene with no receiver does.
    const EmissionPlan plan = build_emission_plan(scene_->sources(), cfg.n_primary_rays);
    if (plan.empty()) {
        // As in the receiver overload: nothing emits, so finalize the (empty) accumulator
        // the caller handed in and report zero rays rather than returning it untouched.
        acc.finalize(cfg.n_primary_rays);
        TraceResult none;
        none.wall_time_s =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        return none;
    }

    int nthreads = cfg.num_threads > 0
                       ? cfg.num_threads
                       : static_cast<int>(std::thread::hardware_concurrency());
    if (nthreads < 1) nthreads = 1;

    // Divide rays evenly into nthreads slots
    std::vector<int> slots(nthreads);
    std::iota(slots.begin(), slots.end(), 0);

    std::size_t base_rays   = cfg.n_primary_rays / static_cast<std::size_t>(nthreads);
    std::size_t extra_rays  = cfg.n_primary_rays % static_cast<std::size_t>(nthreads);

    // One FluxAccumulator per slot; merged after all slots finish
    std::vector<FluxAccumulator> slot_accs;
    slot_accs.reserve(nthreads);
    for (int i = 0; i < nthreads; ++i)
    {
        slot_accs.emplace_back(acc.half_width(), acc.half_height(), acc.nx(), acc.ny());
        slot_accs.back().set_coherent(coherent);
        slot_accs.back().set_coherence_lengths(coherence);
    }

    TraceResult result;
    result.primary_rays_traced = cfg.n_primary_rays;

    // Rays between cancel checks. Hoisted out of the slot loop so the uncontrolled path
    // never touches the control at all.
    const std::size_t check_interval = ctl && ctl->check_interval > 0 ? ctl->check_interval : 4096;
    std::size_t       rays_traced    = 0;

    std::mutex path_mutex;

    std::for_each(std::execution::par, slots.begin(), slots.end(),
                  [&](int slot) {
        std::size_t slot_rays = base_rays + (static_cast<std::size_t>(slot) < extra_rays ? 1 : 0);
        std::size_t ray_start = base_rays * static_cast<std::size_t>(slot) +
                                std::min(static_cast<std::size_t>(slot), extra_rays);

        // Unique seed per slot so slots don't produce identical sequences
        std::uint64_t seed = cfg.rng_seed != 0
                                 ? cfg.rng_seed + static_cast<std::uint64_t>(slot) * 6364136223846793005ULL
                                 : static_cast<std::uint64_t>(
                                       std::chrono::steady_clock::now()
                                           .time_since_epoch()
                                           .count()) +
                                       static_cast<std::uint64_t>(slot) * 6364136223846793005ULL;

        math::Rng slot_rng(seed);
        FluxAccumulator& slot_acc = slot_accs[static_cast<std::size_t>(slot)];
        std::size_t hits = 0;

        std::size_t traced           = 0;    ///< Rays this slot actually traced.
        std::size_t since_last_check = 0;    ///< Rays traced since progress was last published.
        bool        check_due        = true; ///< Poll before the first ray, so a pre-cancelled run does nothing.

        for (std::size_t i = 0; i < slot_rays; ++i) {
            if (ctl && check_due) {
                ctl->add_done(since_last_check);
                since_last_check = 0;
                if (ctl->should_stop()) break;
                check_due = false;
            }

            RayPath* path_ptr = nullptr;
            RayPath  path_buf;

            {
                // Only record paths if budget allows (check under lock, record outside)
                if (cfg.record_paths) {
                    std::lock_guard<std::mutex> lk(path_mutex);
                    if (result.sampled_paths.size() < cfg.max_paths_to_record)
                        path_ptr = &path_buf;
                }
            }

            const EmissionPlan::Entry& e = plan.entry_for(ray_start + i);
            core::Ray r = e.source->sample_ray_indexed(slot_rng, (ray_start + i) - e.first, e.count);
            r.power    *= e.per_ray_w;
            r.id        = static_cast<std::uint32_t>(ray_start + i);
            r.source    = source_index(*scene_, e.source);

            if (path_ptr) {
                path_buf.nodes.push_back(r.origin);   // node 0: where the ray began
                path_buf.monochromatic = (e.source->type_name() == "laser");
            }
            trace_one(r, slot_acc, slot_rng, path_ptr, 0u, cfg.max_bounces,
                      std::min(cfg.power_cutoff_w, cfg.power_cutoff_rel * r.power), hits);

            if (path_ptr && !path_buf.edges.empty()) {
                std::lock_guard<std::mutex> lk(path_mutex);
                if (result.sampled_paths.size() < cfg.max_paths_to_record)
                    result.sampled_paths.push_back(std::move(path_buf));
            }

            ++traced;
            if (ctl && ++since_last_check >= check_interval) check_due = true;
        }

        if (ctl) ctl->add_done(since_last_check);

        // Atomically accumulate hit count
        {
            std::lock_guard<std::mutex> lk(path_mutex);
            result.total_hits += hits;
            rays_traced       += traced;
        }
    });

    // Merge slot accumulators into the output accumulator
    for (auto& sa : slot_accs)
        acc.merge_from(sa);

    // Normalisation stays tied to the *requested* count: each ray already carries
    // total_power / n_primary_rays, so a cancelled run is a correctly weighted partial sum,
    // not a rescaled one. With no cancellation rays_traced == cfg.n_primary_rays exactly.
    acc.finalize(cfg.n_primary_rays);

    result.primary_rays_traced = rays_traced;
    result.cancelled           = ctl && ctl->cancel_requested();

    auto t1 = std::chrono::steady_clock::now();
    result.wall_time_s = std::chrono::duration<double>(t1 - t0).count();

    return result;
}

} // namespace scrt::tracer
