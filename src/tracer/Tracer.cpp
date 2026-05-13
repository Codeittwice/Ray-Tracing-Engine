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
                       std::vector<math::vec3>* path, int max_bounces,
                       double power_cutoff, std::size_t& hit_count) const {
    if (path)
        path->push_back(r.origin);

    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, scrt::math::EPSILON_T,
                               std::numeric_limits<double>::max(), h))
            return;

        if (path)
            path->push_back(h.position);

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
                if (inter.reflected.power >= power_cutoff)
                    trace_one(inter.reflected, acc, rng, path, max_bounces,
                              power_cutoff, hit_count);
                r = inter.transmitted;
                break;
        }
        if (r.power < power_cutoff)
            return;
    }
}

void Tracer::trace_one(core::Ray r, scene::Receiver& receiver, math::Rng& rng,
                       std::vector<math::vec3>* path, int max_bounces,
                       double power_cutoff, std::size_t& hit_count) const {
    if (path)
        path->push_back(r.origin);

    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, scrt::math::EPSILON_T,
                               std::numeric_limits<double>::max(), h))
            return;

        if (path)
            path->push_back(h.position);

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
                    trace_one(inter.reflected, receiver, rng, path, max_bounces,
                              power_cutoff, hit_count);
                r = inter.transmitted;
                break;
        }
        if (r.power < power_cutoff)
            return;
    }
}

TraceResult Tracer::run(const TraceConfig& cfg) const {
    auto* scene_receiver = const_cast<scene::Receiver*>(scene_->receiver());
    if (!scene_receiver)
        return {};

    scene_receiver->clear_accumulators();

    auto t0 = std::chrono::steady_clock::now();

    const auto& sun = *scene_->sun();
    const auto& ap  = scene_->aperture();
    double ray_power = sun.dni() * ap.area() / static_cast<double>(cfg.n_primary_rays);

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

        for (std::size_t i = 0; i < slot_rays; ++i) {
            std::vector<math::vec3>* path_ptr = nullptr;
            std::vector<math::vec3> path_buf;

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

            trace_one(r, slot_receiver, slot_rng, path_ptr, cfg.max_bounces,
                      cfg.power_cutoff_w, hits);

            if (path_ptr && !path_buf.empty()) {
                std::lock_guard<std::mutex> lk(path_mutex);
                if (result.sampled_paths.size() < cfg.max_paths_to_record)
                    result.sampled_paths.push_back(std::move(path_buf));
            }
        }

        {
            std::lock_guard<std::mutex> lk(path_mutex);
            result.total_hits += hits;
        }
    });

    for (auto& receiver : slot_receivers)
        scene_receiver->merge_from(receiver);

    scene_receiver->finalize(cfg.n_primary_rays);

    auto t1 = std::chrono::steady_clock::now();
    result.wall_time_s = std::chrono::duration<double>(t1 - t0).count();

    return result;
}

TraceResult Tracer::run(const TraceConfig& cfg, FluxAccumulator& acc) const {
    auto t0 = std::chrono::steady_clock::now();

    const auto& sun = *scene_->sun();
    const auto& ap  = scene_->aperture();
    double ray_power = sun.dni() * ap.area() / static_cast<double>(cfg.n_primary_rays);

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

        for (std::size_t i = 0; i < slot_rays; ++i) {
            std::vector<math::vec3>* path_ptr = nullptr;
            std::vector<math::vec3> path_buf;

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

            trace_one(r, slot_acc, slot_rng, path_ptr, cfg.max_bounces,
                      cfg.power_cutoff_w, hits);

            if (path_ptr && !path_buf.empty()) {
                std::lock_guard<std::mutex> lk(path_mutex);
                if (result.sampled_paths.size() < cfg.max_paths_to_record)
                    result.sampled_paths.push_back(std::move(path_buf));
            }
        }

        // Atomically accumulate hit count
        {
            std::lock_guard<std::mutex> lk(path_mutex);
            result.total_hits += hits;
        }
    });

    // Merge slot accumulators into the output accumulator
    for (auto& sa : slot_accs)
        acc.merge_from(sa);

    acc.finalize(cfg.n_primary_rays);

    auto t1 = std::chrono::steady_clock::now();
    result.wall_time_s = std::chrono::duration<double>(t1 - t0).count();

    return result;
}

} // namespace scrt::tracer
