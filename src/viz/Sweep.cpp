#include "scrt/viz/Sweep.hpp"

#include "scrt/scene/Receiver.hpp"
#include "scrt/surfaces/Surface.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"

#include <glm/gtc/matrix_transform.hpp>

namespace scrt::viz {

bool SweepRunner::start(scene::Scene& scene, const SweepSpec& spec, const tracer::TraceConfig& cfg) {
    error_.clear();
    frames_.clear();
    running_ = false;
    const auto* surf = scene.surface_by_id(spec.surface_id);
    const auto* recv = scene.receiver();
    if (!surf)                   { error_ = "The part to sweep is not in the scene."; return false; }
    if (!recv)                   { error_ = "The scene has no receiver to record."; return false; }
    if (recv->is_multi_face())   { error_ = "Sweeps record a single-face receiver only."; return false; }
    if (spec.steps < 2)          { error_ = "A sweep needs at least 2 steps."; return false; }
    if (spec.axis < 0 || spec.axis > 2) { error_ = "Axis must be X, Y or Z."; return false; }

    spec_     = spec;
    cfg_      = cfg;
    cfg_.n_primary_rays = spec.rays;
    cfg_.record_paths   = false;   // a sweep wants the maps; paths per frame would be memory for nothing
    original_ = surf->transform();
    pivot_    = surf->world_bounds().centroid();   // turns happen about the part's own centre
    const auto& acc = recv->accumulator();
    nx_ = acc.nx();
    ny_ = acc.ny();
    hw_ = acc.half_width();
    hh_ = acc.half_height();
    frames_.reserve(static_cast<std::size_t>(spec.steps));
    running_ = true;
    return true;
}

core::Transform SweepRunner::pose_at(double value) const {
    math::vec3 axis{0.0};
    axis[spec_.axis] = 1.0;
    const math::mat4 base = original_.matrix();
    if (spec_.kind == SweepSpec::Kind::Move)
        return core::Transform::from_matrix(glm::translate(math::mat4(1.0), value * axis) * base);
    const math::mat4 turn = glm::translate(math::mat4(1.0), pivot_) *
                            glm::rotate(math::mat4(1.0), value, axis) *
                            glm::translate(math::mat4(1.0), -pivot_);
    return core::Transform::from_matrix(turn * base);
}

bool SweepRunner::step(scene::Scene& scene) {
    if (!running_) return true;
    auto* surf = scene.surface_by_id(spec_.surface_id);
    auto* recv = scene.receiver();
    if (!surf || !recv) {
        error_ = "The scene changed under the sweep.";
        running_ = false;
        return true;
    }

    const int    k     = frames_done();
    const double t     = static_cast<double>(k) / static_cast<double>(spec_.steps - 1);
    const double value = spec_.from + t * (spec_.to - spec_.from);
    surf->set_transform(pose_at(value));
    scene.mark_acceleration_dirty();
    scene.build_acceleration_structure();

    tracer::FluxAccumulator acc(hw_, hh_, nx_, ny_);
    try {
        tracer::Tracer(scene).run(cfg_, acc);
    } catch (const std::exception& e) {
        error_ = e.what();
        cancel(scene);
        return true;
    }

    SweepFrame f;
    f.value         = value;
    f.flux          = acc.flux_map_wm2();
    f.total_power_w = acc.total_power_w();
    f.peak_wm2      = acc.peak_flux_wm2();
    f.centre_wm2    = f.flux[static_cast<std::size_t>((ny_ / 2) * nx_ + nx_ / 2)];
    frames_.push_back(std::move(f));

    if (frames_done() >= spec_.steps) {
        cancel(scene);   // restores the pose and the BVH
        return true;
    }
    return false;
}

void SweepRunner::cancel(scene::Scene& scene) {
    if (auto* surf = scene.surface_by_id(spec_.surface_id)) {
        surf->set_transform(original_);
        scene.mark_acceleration_dirty();
        scene.build_acceleration_structure();
    }
    running_ = false;
}

} // namespace scrt::viz
