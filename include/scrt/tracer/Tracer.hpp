#pragma once
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace scrt::scene {
class Receiver;
class Scene;
}

namespace scrt::tracer {

/// Configuration for a tracing run.
struct TraceConfig {
    std::size_t   n_primary_rays       = 1'000'000;
    int           max_bounces          = 16;
    double        power_cutoff_w       = 1e-9;
    std::uint64_t rng_seed             = 0;        ///< 0 → std::random_device
    bool          record_paths         = false;
    std::size_t   max_paths_to_record  = 2000;
    int           num_threads          = 0;        ///< 0 → std::thread::hardware_concurrency
};

/// Summary returned after a completed run.
struct TraceResult {
    std::size_t                           primary_rays_traced = 0;
    std::size_t                           total_hits          = 0;
    double                                wall_time_s         = 0.0;
    std::vector<std::vector<math::vec3>>  sampled_paths;
    bool                                  cancelled           = false; ///< Stopped early on request.
};

/// Cancellation and progress channel for one in-flight run; opt-in, never owned by the tracer.
///
/// The tracer polls this from every worker slot, so every member must stay thread-safe. It is
/// entirely optional: passing no control leaves `Tracer::run` byte-for-byte the run it always
/// was, which is what keeps the headless and comparison workflows fixed.
class TraceControl {
public:
    /// Arms the channel for a run of `total` primary rays (clears cancel and progress).
    void reset(std::size_t total) noexcept {
        cancel_.store(false, std::memory_order_relaxed);
        done_.store(0, std::memory_order_relaxed);
        total_.store(total, std::memory_order_relaxed);
    }

    /// Asks the running trace to stop at its next check point; callable from any thread.
    void request_cancel() noexcept { cancel_.store(true, std::memory_order_relaxed); }

    /// True once cancellation has been requested, by flag or by poll hook.
    bool cancel_requested() const noexcept { return cancel_.load(std::memory_order_relaxed); }

    /// Primary rays completed so far, republished at every check point.
    std::size_t rays_done() const noexcept { return done_.load(std::memory_order_relaxed); }

    /// Primary rays this run was asked for, as handed to reset().
    std::size_t rays_total() const noexcept { return total_.load(std::memory_order_relaxed); }

    /// Completed fraction in [0,1]; 0 while the total is unknown.
    double progress() const noexcept {
        const std::size_t t = rays_total();
        if (t == 0) return 0.0;
        const double f = static_cast<double>(rays_done()) / static_cast<double>(t);
        return f > 1.0 ? 1.0 : f;
    }

    /// Publishes `n` newly completed rays; called by the tracer, not by the GUI.
    void add_done(std::size_t n) noexcept { done_.fetch_add(n, std::memory_order_relaxed); }

    /// True when the run must stop now: consults the cancel flag, then the poll hook.
    bool should_stop() {
        if (cancel_.load(std::memory_order_relaxed)) return true;
        if (poll_hook && poll_hook()) {
            cancel_.store(true, std::memory_order_relaxed);
            return true;
        }
        return false;
    }

    /// Primary rays a worker slot traces between checks: latency against per-ray overhead.
    std::size_t check_interval = 4096;

    /// Optional extra cancel predicate polled at each check; must be thread-safe. Left null by
    /// the GUI; tests use it to cancel a run deterministically instead of racing a timer.
    std::function<bool()> poll_hook;

private:
    std::atomic<bool>        cancel_{false};
    std::atomic<std::size_t> done_{0};
    std::atomic<std::size_t> total_{0};
};

/// Multi-threaded Monte Carlo ray tracer using std::execution::par.
class Tracer {
public:
    explicit Tracer(const scene::Scene& s) : scene_(&s) {}

    /// Trace into the scene receiver's own face accumulators; `ctl` is optional.
    TraceResult run(const TraceConfig& cfg, TraceControl* ctl = nullptr) const;

    /// Trace into a caller-provided single receiver accumulator; `ctl` is optional.
    TraceResult run(const TraceConfig& cfg, FluxAccumulator& acc,
                    TraceControl* ctl = nullptr) const;

private:
    void trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                   std::vector<math::vec3>* path_out, int max_bounces,
                   double power_cutoff, std::size_t& hit_count) const;

    void trace_one(core::Ray r, scene::Receiver& acc, math::Rng& rng,
                   std::vector<math::vec3>* path_out, int max_bounces,
                   double power_cutoff, std::size_t& hit_count) const;

    const scene::Scene* scene_;
};

} // namespace scrt::tracer
