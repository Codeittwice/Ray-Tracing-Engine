#pragma once
#include "scrt/io/SceneDocument.hpp"
#include "scrt/surfaces/Surface.hpp"
#include <filesystem>
#include <memory>

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

/// Constructs the surface geometry for one element or receiver plane, dispatching on SurfaceDoc's
/// active alternative; a mesh path resolves against `base_dir`.
///
/// Public because scene::SceneEditor derives surfaces from the same documents the loader does, and
/// the two must never disagree about what an ElementDoc means. It was a file-local helper with a
/// hand-maintained copy in SceneEditor.cpp, which meant a new surface type had to be added in two
/// places or the editor would silently fail to build what the loader could.
std::unique_ptr<surfaces::Surface> build_surface(const SurfaceDoc& sd,
                                                 const std::filesystem::path& base_dir);

/// True when every field of `t` is still its struct default, i.e. the element has no transform.
///
/// Callers must branch on this rather than always calling core::Transform::from_trs(): an element
/// with no transform has to keep the surface's default-constructed core::Transform, so that a
/// document-derived surface is bitwise identical to a loader-derived one. Public for the same
/// reason as build_surface() above.
bool is_default_transform(const TransformDoc& t);

} // namespace scrt::io
