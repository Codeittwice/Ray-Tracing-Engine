#pragma once

#include "scrt/core/Transform.hpp"
#include "scrt/math/Vec.hpp"
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/Tracer.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace scrt::viz {

/// What to sweep: one part, moved along or turned about a world axis, over a range of values.
struct SweepSpec {
    enum class Kind { Move, Turn };
    std::uint64_t surface_id = 0;
    Kind          kind       = Kind::Move;
    int           axis       = 0;        ///< 0 = world X, 1 = Y, 2 = Z.
    double        from       = 0.0;      ///< Metres for Move, radians for Turn, relative to the start pose.
    double        to         = 0.0;
    int           steps      = 30;       ///< Frames, including both ends (>= 2).
    std::size_t   rays       = 100000;   ///< Primary rays per frame.
};

/// One traced frame of a sweep.
struct SweepFrame {
    double              value = 0.0;          ///< The swept offset for this frame (m or rad).
    std::vector<double> flux;                 ///< Receiver flux map [W/m2], row-major.
    double              total_power_w = 0.0;
    double              peak_wm2      = 0.0;
    double              centre_wm2    = 0.0;  ///< Flux in the receiver's centre bin.
};

/// Traces a part through a range of poses, one frame per step() call, and restores the original pose.
///
/// Pure (Scene + Tracer, no GUI), so it runs in tests and on the GUI thread alike. It MUTATES the
/// scene's live surface transform while it runs - the caller must not trace the same scene on another
/// thread meanwhile - and never the document: a sweep explores, it does not edit.
class SweepRunner {
public:
    /// Prepares a sweep; returns false with `error()` set when it cannot run (no such part, no
    /// receiver, a multi-face receiver, fewer than 2 steps). `cfg` supplies bounces, seed and cutoff.
    bool start(scene::Scene& scene, const SweepSpec& spec, const tracer::TraceConfig& cfg);

    /// Traces the next frame. Returns true once the last frame is done (the pose is then restored).
    bool step(scene::Scene& scene);

    /// Stops early and restores the original pose; the frames traced so far are kept.
    void cancel(scene::Scene& scene);

    bool running() const { return running_; }
    int  frames_done() const { return static_cast<int>(frames_.size()); }
    int  frames_total() const { return spec_.steps; }
    const std::vector<SweepFrame>& frames() const { return frames_; }
    const SweepSpec&               spec() const { return spec_; }
    const std::string&             error() const { return error_; }

    /// The world transform of the swept part at offset `value` (for playback).
    core::Transform pose_at(double value) const;
    /// The part's transform before the sweep started.
    const core::Transform& original_pose() const { return original_; }

    int    nx() const { return nx_; }
    int    ny() const { return ny_; }
    double half_width() const { return hw_; }
    double half_height() const { return hh_; }

private:
    SweepSpec               spec_;
    tracer::TraceConfig     cfg_;
    core::Transform         original_;
    math::vec3              pivot_{0.0};
    std::vector<SweepFrame> frames_;
    std::string             error_;
    bool                    running_ = false;
    int                     nx_ = 0, ny_ = 0;
    double                  hw_ = 0.0, hh_ = 0.0;
};

} // namespace scrt::viz
