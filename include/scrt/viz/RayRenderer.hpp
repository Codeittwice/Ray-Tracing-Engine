#pragma once
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/Tracer.hpp"

namespace scrt::viz {

/// Registers scene geometry and ray paths with Polyscope.
class RayRenderer {
public:
    explicit RayRenderer(const scene::Scene* scene) : scene_(scene) {}

    /// Tessellate and register all optical surfaces as Polyscope meshes.
    void register_surfaces(int tess_segs = 32);

    /// Register the collection aperture as a translucent disk.
    void register_aperture();

    /// Register sampled ray paths as a Polyscope curve network.
    void register_paths(const tracer::TraceResult& result);

    /// Remove all previously registered geometry.
    void clear();

private:
    const scene::Scene* scene_;
};

} // namespace scrt::viz
