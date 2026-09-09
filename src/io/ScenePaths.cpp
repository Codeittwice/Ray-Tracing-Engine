#include "scrt/io/ScenePaths.hpp"
#include "scrt/io/SceneDocument.hpp"
#include <cstdio>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace scrt::io {

using json = nlohmann::json;

namespace {

/// True when `a` and `b` name the same directory. Tries a canonical comparison first (so
/// symlinks/`..`/case-insensitive Windows drive letters agree); falls back to a purely lexical
/// comparison when either path does not exist yet, since `new_base` may be a not-yet-created
/// Save-As destination and `weakly_canonical` failing here must not be treated as "different".
bool same_directory(const std::filesystem::path& a, const std::filesystem::path& b) {
    try {
        if (std::filesystem::weakly_canonical(a) == std::filesystem::weakly_canonical(b))
            return true;
    } catch (const std::filesystem::filesystem_error&) {
        // Fall through to the lexical comparison below.
    }
    return a.lexically_normal() == b.lexically_normal();
}

/// Rewrites `["surface"]["path"]` in place for one element JSON object, if it is a mesh surface
/// with a string path. Every access is guarded so a malformed or unexpected element shape is
/// skipped rather than throwing.
void rebase_one_element(json& element, const std::filesystem::path& old_base,
                        const std::filesystem::path& new_base) {
    if (!element.is_object() || !element.contains("surface"))
        return;
    json& surface = element["surface"];
    if (!surface.is_object() || !surface.contains("type") || !surface["type"].is_string())
        return;
    if (surface["type"].get<std::string>() != "mesh")
        return;
    if (!surface.contains("path") || !surface["path"].is_string())
        return;

    const std::string rebased =
        rebase_relative_path(surface["path"].get<std::string>(), old_base, new_base);
    surface["path"] = rebased;
}

/// Walks `root["scene"]["elements"]` rewriting every mesh surface path from `old_base` to
/// `new_base`. Silently does nothing if `root` does not have the expected shape (no "scene" key,
/// no "elements" array, etc.) rather than throwing — a document with no elements is common and
/// legitimate.
void rebase_mesh_paths(json& root, const std::filesystem::path& old_base,
                       const std::filesystem::path& new_base) {
    if (!root.is_object() || !root.contains("scene"))
        return;
    json& scene = root["scene"];
    if (!scene.is_object() || !scene.contains("elements") || !scene["elements"].is_array())
        return;
    for (json& element : scene["elements"])
        rebase_one_element(element, old_base, new_base);
}

/// Writes `text` to `path` via a same-directory `.tmp` file followed by an atomic rename, so a
/// crash or a full disk mid-write cannot destroy an existing scene file at `path`. Creates
/// `path`'s parent directory if needed. Throws std::runtime_error (naming `path`) on any failure,
/// cleaning up the temp file first.
void write_file_atomically(const std::filesystem::path& path, const std::string& text) {
    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        // Ignore ec here: if the directory genuinely can't be created, the subsequent file open
        // below will fail and we report that (with a clearer, path-specific message).
    }

    std::filesystem::path tmp = path;
    tmp += ".tmp";

    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) {
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error("save_scene: cannot open temp file for '" + path.string() + "'");
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out) {
            out.close();
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error("save_scene: write failed (disk full?) for '" + path.string() + "'");
        }
        out.close();
        if (!out) {
            std::filesystem::remove(tmp, ec);
            throw std::runtime_error("save_scene: failed to close temp file for '" + path.string() + "'");
        }
    }

    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        std::filesystem::remove(tmp, ec);
        throw std::runtime_error("save_scene: failed to replace '" + path.string() + "': " + ec.message());
    }
}

} // namespace

std::string rebase_relative_path(const std::string& stored_path,
                                  const std::filesystem::path& old_base,
                                  const std::filesystem::path& new_base) {
    if (stored_path.empty())
        return stored_path;

    const std::filesystem::path stored(stored_path);
    if (stored.is_absolute())
        return stored_path;

    if (same_directory(old_base, new_base))
        return stored_path;

    const std::filesystem::path target = old_base / stored;

    try {
        std::filesystem::path rel = std::filesystem::relative(target, new_base);
        if (!rel.empty())
            return rel.generic_string();
    } catch (const std::filesystem::filesystem_error&) {
        // Fall through to the absolute fallback below (e.g. target/new_base on different
        // volumes on Windows, which std::filesystem::relative can throw on).
    }

    // No relative path exists (or relative() threw) - most commonly a cross-volume move on
    // Windows (C:\... -> D:\...). Fall back to an absolute path so the scene still loads.
    std::filesystem::path fallback;
    try {
        fallback = std::filesystem::weakly_canonical(target);
    } catch (const std::filesystem::filesystem_error&) {
        std::error_code ec;
        fallback = std::filesystem::absolute(target, ec);
        if (ec)
            fallback = target;
    }

    std::cerr << "scrt::io::rebase_relative_path: no relative path from '" << new_base.string()
              << "' to '" << target.string() << "'; storing an absolute path instead ('"
              << fallback.generic_string() << "')" << std::endl;

    return fallback.generic_string();
}

void save_scene_as(const SceneDocument& doc, const std::filesystem::path& path,
                    const std::filesystem::path& old_base) {
    json root = write_document(doc);

    const std::filesystem::path new_base = path.parent_path();
    rebase_mesh_paths(root, old_base, new_base);

    std::string text = root.dump(2);
    text += '\n';

    write_file_atomically(path, text);
}

void save_scene(const SceneDocument& doc, const std::filesystem::path& path) {
    // The frozen SceneDocument.hpp signature gives save_scene() no way to know the directory the
    // document was originally loaded from - that information simply isn't part of its
    // parameters. So this entry point can only implement the always-safe case: a plain "Save"
    // back to the same file, where old_base == new_base == path.parent_path() and
    // rebase_relative_path() is a byte-for-byte no-op for every mesh path. It does NOT and
    // cannot repair mesh references for a Save-As into a different directory - callers that know
    // the original load directory (Wave 3+ editor) must call save_scene_as(doc, path, old_base)
    // instead, which does the real rebase.
    save_scene_as(doc, path, path.parent_path());
}

} // namespace scrt::io
