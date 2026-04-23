#pragma once
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/FluxAccumulator.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>

namespace scrt::io {

/// Write flux map as row-major CSV (one row per y bin, W/m² values).
void export_flux_csv(const tracer::FluxAccumulator& acc,
                     const std::filesystem::path& out);

/// Write flux map as a NumPy .npy file (float64, shape [ny, nx]).
void export_flux_npy(const tracer::FluxAccumulator& acc,
                     const std::filesystem::path& out);

/// Write a JSON summary: total power, peak flux, concentration ratio, MC wall time.
void export_summary_json(const tracer::FluxAccumulator& acc,
                         const tracer::TraceResult& result,
                         double dni_wm2,
                         const std::filesystem::path& out);

/// Write tessellated scene geometry as a Wavefront OBJ file (for debugging in Blender).
void export_scene_obj(const scene::Scene& scene, int nseg,
                      const std::filesystem::path& out);

} // namespace scrt::io
