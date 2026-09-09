#pragma once
#include "scrt/io/SceneDocument.hpp"
#include <filesystem>
#include <string>

namespace scrt::io {

/// Rewrites a scene-relative path so it still resolves when the scene file moves to a new
/// directory. Absolute inputs and empty inputs pass through unchanged; `old_base == new_base`
/// is a no-op; otherwise the path is recomputed relative to `new_base`, falling back to an
/// absolute path (with a warning to std::cerr) when no relative path exists, e.g. a cross-volume
/// move on Windows. See ScenePaths.cpp for the full case-by-case contract.
std::string rebase_relative_path(const std::string& stored_path,
                                  const std::filesystem::path& old_base,
                                  const std::filesystem::path& new_base);

/// Serializes `doc` and writes it to `path`, rebasing any relative mesh paths from `old_base` so
/// they still resolve from `path`'s directory. This is the real Save-As entry point: it is the
/// only function in this file that can actually repair mesh references when the scene moves to a
/// different directory, because it is the only one told where the document was loaded from.
/// Writes atomically via a same-directory temporary file + rename (see ScenePaths.cpp).
void save_scene_as(const SceneDocument& doc, const std::filesystem::path& path,
                    const std::filesystem::path& old_base);

} // namespace scrt::io
