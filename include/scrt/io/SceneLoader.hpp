#pragma once
#include "scrt/scene/Scene.hpp"
#include "scrt/tracer/Tracer.hpp"
#include <filesystem>
#include <memory>

namespace scrt::io {

/// Parsed scene plus trace configuration returned by load_scene().
struct LoadedScene {
    std::unique_ptr<scene::Scene> scene;
    tracer::TraceConfig           cfg;
};

/// Parse a JSON scene file (§5.10 schema) and return a fully wired Scene.
/// Throws std::runtime_error on any parse or validation error.
LoadedScene load_scene(const std::filesystem::path& path);

} // namespace scrt::io
