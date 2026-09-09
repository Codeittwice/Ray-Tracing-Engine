#pragma once
#include "scrt/io/SceneDocument.hpp"

// write_document() is declared in the frozen SceneDocument.hpp (owned by another Wave 2 agent)
// and implemented in SceneWriter.cpp. No additional public writer helpers are needed today; all
// serialization helpers live in an anonymous namespace in SceneWriter.cpp.
//
// save_scene(), also declared in SceneDocument.hpp, is deliberately NOT defined in
// SceneWriter.cpp: ScenePaths.cpp already defines it (as a thin wrapper over save_scene_as(),
// which owns the mesh-path rebasing and the temp-file + rename atomic write). Defining it here
// as well would be a duplicate symbol at link time. Save-As callers that know the directory the
// document was loaded from should call save_scene_as() from ScenePaths.hpp directly, because
// save_scene()'s frozen signature cannot express the original base directory.
