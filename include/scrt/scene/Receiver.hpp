#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"

namespace scrt::scene {

/// A flat receiver surface with an attached flux-accumulation grid.
class Receiver {
public:
    Receiver(double half_width, double half_height, int nx, int ny);

    surfaces::Plane*              surface()     { return &plane_; }
    const surfaces::Plane*        surface()     const { return &plane_; }
    tracer::FluxAccumulator&      accumulator() { return acc_; }
    const tracer::FluxAccumulator& accumulator() const { return acc_; }

    /// Forwards the transform to the underlying plane.
    void set_transform(core::Transform t);

private:
    surfaces::Plane         plane_;
    tracer::FluxAccumulator acc_;
};

} // namespace scrt::scene
