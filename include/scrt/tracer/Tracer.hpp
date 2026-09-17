#pragma once
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/math/Rng.hpp"
#include "scrt/math/Vec.hpp"
#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <complex>
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
    /// A branch is also kept while it carries at least this FRACTION of its primary ray's starting
    /// power: the cutoff actually applied is min(power_cutoff_w, power_cutoff_rel x start power).
    /// The absolute 1e-9 W was sized for solar rays of ~0.03 W and silently dropped every ghost and
    /// extinction leak of a milliwatt laser split over 1e5 rays (2.5e-8 W each).
    double        power_cutoff_rel     = 1e-6;
    std::uint64_t rng_seed             = 0;        ///< 0 → std::random_device
    bool          record_paths         = false;
    std::size_t   max_paths_to_record  = 2000;
    int           num_threads          = 0;        ///< 0 → std::thread::hardware_concurrency
};

/// One primary ray's full history, as a TREE rather than a polyline.
///
/// A splitting surface sends a ray two ways at once, so a single sequence of points cannot
/// describe what happened. It used to be one: both branches appended to the same vector, and the
/// renderer drew consecutive points, so a split path was drawn as a line that ran out along the
/// reflected branch and then teleported back to the split point to continue transmitted. With a
/// beam splitter that is not a cosmetic flaw, it is a picture of something that did not happen.
///
/// INVARIANT: `edges` forms a tree rooted at node 0 (the emission point). Every node except 0
/// appears exactly once as an edge's `to`.
/// What the light was doing along one recorded path edge - for drawing and for the ray inspector.
/// Recorded, never read by the physics.
struct RayEdgeInfo {
    double               wavelength_nm = 550.0;
    double               power_w       = 0.0;
    double               opl_end_m     = 0.0;   ///< Optical path length at the edge's END [m].
    bool                 polarised     = false;
    math::vec3           direction     {0.0, 0.0, 1.0};
    math::vec3           s_axis        {0.0, 1.0, 0.0};
    std::complex<double> Es, Ep;                ///< Jones vector in (s_axis, direction x s_axis).
};

struct RayPath {
    std::vector<math::vec3>                   nodes;         ///< nodes[0] is where the ray began.
    std::vector<std::array<std::uint32_t, 2>> edges;         ///< (from, to) indices into nodes.
    /// Power carried along each edge [W], parallel to `edges`. The only way a reader can tell a
    /// 4%-reflected branch from a 96%-transmitted one, which is the point of looking at a beam
    /// splitter at all.
    std::vector<double>                       edge_power_w;
    /// Per edge, parallel to `edges`: wavelength, polarisation state and optical path.
    std::vector<RayEdgeInfo>                  edge_info;
    /// True for a single-wavelength source (a laser): its rays are drawn in their wavelength's
    /// colour. Sunlight is broadband, and one colour for it would misrepresent it.
    bool                                      monochromatic = false;
};

/// Summary returned after a completed run.
struct TraceResult {
    std::size_t          primary_rays_traced = 0;
    std::size_t          total_hits          = 0;
    double               wall_time_s         = 0.0;
    std::vector<RayPath> sampled_paths;
    bool                 cancelled           = false; ///< Stopped early on request.
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
    /// `parent_node` is the index in `path_out->nodes` this ray leaves from; a split's reflected
    /// branch is recursed with the split point as its parent, which is what makes the record a
    /// tree. Ignored when `path_out` is null.
    void trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                   RayPath* path_out, std::uint32_t parent_node, int max_bounces,
                   double power_cutoff, std::size_t& hit_count) const;

    void trace_one(core::Ray r, scene::Receiver& acc, math::Rng& rng,
                   RayPath* path_out, std::uint32_t parent_node, int max_bounces,
                   double power_cutoff, std::size_t& hit_count) const;

    const scene::Scene* scene_;
};

} // namespace scrt::tracer
