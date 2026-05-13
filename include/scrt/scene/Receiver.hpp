#pragma once
#include "scrt/core/Transform.hpp"
#include "scrt/surfaces/Plane.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace scrt::scene {

/// Controls whether a receiver face stops or transmits a ray after recording flux.
enum class ReceiverFaceMode {
    RecordAbsorb,
    RecordPass
};

/// One planar face of a receiver with its own flux-accumulation grid.
class ReceiverFace {
public:
    /// Create a named planar receiver face.
    ReceiverFace(std::string name, double half_width, double half_height,
                 int nx, int ny, ReceiverFaceMode mode);

    /// Surface used for ray intersection.
    surfaces::Plane* surface() { return &plane_; }
    /// Surface used for ray intersection.
    const surfaces::Plane* surface() const { return &plane_; }
    /// Flux accumulator for this face.
    tracer::FluxAccumulator& accumulator() { return acc_; }
    /// Flux accumulator for this face.
    const tracer::FluxAccumulator& accumulator() const { return acc_; }
    /// Stable face name used in exports.
    const std::string& name() const { return name_; }
    /// Rename this face and its underlying surface.
    void set_name(std::string name);
    /// Interaction mode after flux is recorded.
    ReceiverFaceMode mode() const { return mode_; }
    /// Set the interaction mode after flux is recorded.
    void set_mode(ReceiverFaceMode mode) { mode_ = mode; }
    /// Set the underlying plane transform.
    void set_transform(core::Transform t);

private:
    std::string              name_;
    ReceiverFaceMode         mode_;
    surfaces::Plane          plane_;
    tracer::FluxAccumulator  acc_;
};

/// A receiver made of one or more planar faces with flux grids.
class Receiver {
public:
    /// Create a legacy single-plane absorbing receiver.
    Receiver(double half_width, double half_height, int nx, int ny);

    /// Add a receiver face and return it.
    ReceiverFace& add_face(std::string name, double half_width, double half_height,
                           int nx, int ny, ReceiverFaceMode mode);

    /// Create an empty receiver with the same face names, dimensions, grids, and modes.
    Receiver clone_empty() const;

    /// Return all receiver faces.
    std::span<const std::unique_ptr<ReceiverFace>> faces() const { return faces_; }
    /// Return all receiver faces.
    std::span<std::unique_ptr<ReceiverFace>> mutable_faces() { return faces_; }

    /// Find the face index whose surface pointer matches surface, or -1.
    int face_index_for_surface(const surfaces::Surface* surface) const;

    /// Reset every face accumulator to zero.
    void clear_accumulators() noexcept;

    /// Merge another receiver's face accumulators into this one.
    void merge_from(const Receiver& other) noexcept;

    /// Finalize every face accumulator.
    void finalize(std::size_t total_primary_rays);

    /// True when this receiver contains more than the legacy single face.
    bool is_multi_face() const { return faces_.size() > 1; }

    surfaces::Plane*              surface()     { return faces_.front()->surface(); }
    const surfaces::Plane*        surface()     const { return faces_.front()->surface(); }
    tracer::FluxAccumulator&      accumulator() { return faces_.front()->accumulator(); }
    const tracer::FluxAccumulator& accumulator() const { return faces_.front()->accumulator(); }

    /// Forwards the transform to the underlying plane.
    void set_transform(core::Transform t);

private:
    std::vector<std::unique_ptr<ReceiverFace>> faces_;
};

} // namespace scrt::scene
