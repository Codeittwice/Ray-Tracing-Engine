#include "scrt/tracer/Tracer.hpp"
#include "scrt/math/Constants.hpp"
#include "scrt/materials/Material.hpp"
#include "scrt/scene/Receiver.hpp"
#include "scrt/scene/Scene.hpp"
#include <algorithm>
#include <chrono>
#include <execution>
#include <limits>
#include <mutex>
#include <numeric>
#include <thread>
#include <vector>

namespace scrt::tracer {

void Tracer::trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                       RayPath* path, std::uint32_t parent_node, int max_bounces,
                       double power_cutoff, std::size_t& hit_count) const {
    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, scrt::math::EPSILON_T,
                               std::numeric_limits<double>::max(), h))
            return;

        if (path) {
            // One node per hit, one edge from wherever this ray came from. Recording the edge's
            // power here (before the material acts) is what lets the renderer show how much
            // light each branch of a split actually carries.
            const auto node = static_cast<std::uint32_t>(path->nodes.size());
            path->nodes.push_back(h.position);
            path->edges.push_back({parent_node, node});
            path->edge_power_w.push_back(r.power);
            parent_node = node;
        }

        ++hit_count;

        auto inter = h.surface->material()->interact(r, h, rng);
        switch (inter.kind) {
            case materials::InteractionKind::Absorbed:
                acc.deposit(r, h);
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

        if (path) {
            // One node per hit, one edge from wherever this ray came from. Recording the edge's
            // power here (before the material acts) is what lets the renderer show how much
            // light each branch of a split actually carries.
            const auto node = static_cast<std::uint32_t>(path->nodes.size());
            path->nodes.push_back(h.position);
            path->edges.push_back({parent_node, node});
            path->edge_power_w.push_back(r.power);
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
            if (r.power < power_cutoff)
                return;
            continue;
        }

        auto inter = h.surface->material()->interact(r, h, rng);
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

    auto t0 = std::chrono::steady_clock::now();

    const auto& sun = *scene_->sun();
    const auto& ap  = scene_->aperture();
    // DNI is per unit area NORMAL to the beam, so the aperture only intercepts its
    // foreshortened projection. sun_direction() is where light travels; the sun is the
    // other way. Zenith scenes (normal = +Z, direction = -Z) give cos_ap == 1 exactly.
    const double cos_ap = ap.cosine_to(-sun.sun_direction());
    double ray_power = sun.dni() * ap.area() * cos_ap / static_cast<double>(cfg.n_primary_rays);

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

            core::Ray r = sun.sample_ray(ap, slot_rng);
            r.power     = ray_power;
            r.id        = static_cast<std::uint32_t>(ray_start + i);

            if (path_ptr) path_buf.nodes.push_back(r.origin);   // node 0: where the ray began
            trace_one(r, slot_receiver, slot_rng, path_ptr, 0u, cfg.max_bounces,
                      cfg.power_cutoff_w, hits);

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

    const auto& sun = *scene_->sun();
    const auto& ap  = scene_->aperture();
    // DNI is per unit area NORMAL to the beam, so the aperture only intercepts its
    // foreshortened projection. sun_direction() is where light travels; the sun is the
    // other way. Zenith scenes (normal = +Z, direction = -Z) give cos_ap == 1 exactly.
    const double cos_ap = ap.cosine_to(-sun.sun_direction());
    double ray_power = sun.dni() * ap.area() * cos_ap / static_cast<double>(cfg.n_primary_rays);

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
        slot_accs.emplace_back(acc.half_width(), acc.half_height(), acc.nx(), acc.ny());

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

            core::Ray r = sun.sample_ray(ap, slot_rng);
            r.power     = ray_power;
            r.id        = static_cast<std::uint32_t>(ray_start + i);

            if (path_ptr) path_buf.nodes.push_back(r.origin);   // node 0: where the ray began
            trace_one(r, slot_acc, slot_rng, path_ptr, 0u, cfg.max_bounces,
                      cfg.power_cutoff_w, hits);

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
