#pragma once
#include "scrt/io/SceneDocument.hpp"
#include <filesystem>

// LoadedScene used to be declared here and SceneDocument.hpp included this file to see it. That
// direction inverted when LoadedScene gained a SceneDocument member: the struct now lives in
// SceneDocument.hpp beside build_scene(), its only other producer, and this header includes that
// one. Existing translation units that include only SceneLoader.hpp still see Scene, TraceConfig
// and LoadedScene, transitively.

namespace scrt::io {

/// Parse a JSON scene file (§5.10 schema) and return a fully wired Scene together with the
/// SceneDocument it was parsed from, so the result can be re-saved without re-reading the file.
/// Throws std::runtime_error on any parse or validation error.
LoadedScene load_scene(const std::filesystem::path& path);

} // namespace scrt::io
