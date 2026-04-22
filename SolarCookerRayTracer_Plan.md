# SolarCookerRayTracer_Plan.md

> **NOTE TO HUMAN USER:** This document is a complete brief for Claude Code. Drop this single file into an empty project folder (e.g. `C:\dev\solar-cooker-rt\SolarCookerRayTracer_Plan.md`), open the folder in VS Code, then paste the kickoff prompt at the very bottom of this file ("HUMAN: HOW TO LAUNCH") into the Claude Code chat. Claude Code will read this file, set up everything else (CLAUDE.md, .gitignore, .clang-format, CMakeLists.txt, vcpkg.json, source skeleton, tests), and begin executing Phase 1. Before that, you must have completed the **PREREQUISITES** section just below.
>
> All sections after PREREQUISITES are written **for Claude Code**.

---

## PREREQUISITES (for the human, ~45 minutes, one-time)

Skip this section if you've already done it. Everything in it is needed *before* Claude Code can act.

### 1. Install Visual Studio Community or Build Tools for Visual Studio (latest stable release)
- Download from <https://visualstudio.microsoft.com/downloads/>.
- In the installer, check the **"Desktop development with C++"** workload. Default sub-components are fine.
- Install (~8 GB), then **reboot Windows**.

### 2. Install command-line tools via winget
Open **PowerShell as Administrator** and run:
```powershell
winget install --id Git.Git -e
winget install --id Kitware.CMake -e
winget install --id OpenJS.NodeJS.LTS -e
winget install --id Python.Python.3.12 -e
winget install --id Ninja-build.Ninja -e
```
Close PowerShell.

### 3. Install vcpkg
Open **"x64 Native Tools Command Prompt for VS 2022"** from the Start menu (pin it to your taskbar — you'll use it often). Run:
```cmd
mkdir C:\dev
cd C:\dev
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat
```
Then in **PowerShell** (regular, not admin), set the env var permanently:
```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\dev\vcpkg", "User")
```
**Close every terminal window** so the new variable takes effect.

### 4. Install VS Code and extensions
- Install VS Code from <https://code.visualstudio.com/download> (User Installer, 64-bit). Tick all "Add to PATH" / context-menu options during install.
- Open VS Code. Press `Ctrl+Shift+X`. Install these extensions by exact name:
  - **Claude Code** (publisher: Anthropic)
  - **C/C++** (publisher: Microsoft)
  - **CMake Tools** (publisher: Microsoft)
  - **clangd** (publisher: LLVM)
  - **clang-format** (publisher: xaver)
  - **GitLens** (publisher: GitKraken)
- Open Settings (`Ctrl+,`), search `C_Cpp.intelliSenseEngine`, set it to **disabled** (so clangd owns IntelliSense).
- Click the **Spark icon** in the Activity Bar → **Sign in to Claude** with your Pro/Max/Team account.

### 5. Create the project folder and open it
In the x64 Native Tools Command Prompt:
```cmd
cd C:\dev
mkdir solar-cooker-rt
cd solar-cooker-rt
git init
code .
```
When VS Code asks "Do you trust the authors..." click **Yes**.

### 6. Drop this file into the folder
Save this entire `SolarCookerRayTracer_Plan.md` into the project root (`C:\dev\solar-cooker-rt\`).

### 7. Launch Claude Code
Click the Spark icon. Toggle **Plan Mode ON** in the Claude Code panel toolbar. Paste the prompt at the very bottom of this file (the "HUMAN: HOW TO LAUNCH" section) into the chat.

That's it for you. Claude Code takes over from there.

---

# BRIEF FOR CLAUDE CODE — START READING HERE

You are building a 3D Monte Carlo optical ray tracer specialized for solar cooker design. You will work through the project in seven phases, executing each phase only after the previous phase's tests pass. This document is your single source of truth — refer back to specific section numbers when planning.

## 0. Bootstrap tasks (do these immediately, before Phase 1)

Before writing any source code, do the following in order. Pause for human approval after each major step.

### 0.1 Create `CLAUDE.md` in the project root with this exact content:

```markdown
# Project: Solar Cooker Ray Tracer (C++ / CMake / vcpkg)

## Source of truth
Follow `SolarCookerRayTracer_Plan.md` exactly. That document specifies:
- Repository layout (Section 1)
- Dependencies and vcpkg manifest (Section 2)
- Build configuration (Section 3)
- Coding conventions (Section 4) — obey strictly
- Module specifications (Section 5)
- Validation tests T1-T12 (Section 9)
- Phased build order (Section 10)

## Rules
- Execute phases in order. Do not skip ahead.
- After each phase, run the acceptance tests before proposing the next phase.
- All geometry and physics in `double` precision (`glm::dvec3`, no custom vector types).
- SI units throughout (meters, watts, radians; nanometers only for wavelengths in I/O).
- No `shared_ptr` unless truly required.
- Every public class/function gets a one-line Doxygen comment.
- If unsure about a design decision, ask the user before coding.

## Current phase
Phase 1 — Bootstrap. In progress.

## Platform
Windows 10/11, MSVC (current stable release, x64), CMake, Ninja generator, vcpkg at C:\dev\vcpkg in manifest mode. VCPKG_ROOT environment variable is set.

## Working style
- Plan Mode is ON. Propose plans before multi-file changes.
- Show diffs before writing large new files.
- After meaningful changes, run `cmake --build build/debug` and `ctest --test-dir build/debug --output-on-failure`. Report results.
- Commit in logical units with descriptive messages.
```

### 0.2 Create `.gitignore` in the project root:

```
# Build
build/
out/
cmake-build-*/

# vcpkg
vcpkg_installed/

# IDE
.vscode/*.log
.vs/
.idea/
*.user

# OS
Thumbs.db
desktop.ini
.DS_Store

# Compiled
*.o
*.obj
*.exe
*.dll
*.so
*.dylib
*.a
*.lib
*.pdb
*.ilk
*.exp
```

### 0.3 Create `.clang-format` in the project root:

```yaml
---
BasedOnStyle: LLVM
Language: Cpp
Standard: c++20
IndentWidth: 4
ColumnLimit: 100
PointerAlignment: Left
AllowShortFunctionsOnASingleLine: Inline
AllowShortIfStatementsOnASingleLine: WithoutElse
BreakBeforeBraces: Attach
NamespaceIndentation: None
SortIncludes: CaseSensitive
IncludeBlocks: Regroup
SpaceAfterTemplateKeyword: false
AlwaysBreakTemplateDeclarations: Yes
```

### 0.4 Create `.vscode/settings.json`:

```json
{
  "cmake.configureSettings": {
    "CMAKE_TOOLCHAIN_FILE": "C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"
  },
  "cmake.generator": "Ninja",
  "cmake.buildDirectory": "${workspaceFolder}/build/${buildType}",
  "C_Cpp.intelliSenseEngine": "disabled",
  "editor.formatOnSave": true,
  "editor.defaultFormatter": "xaver.clang-format",
  "files.associations": { "*.hpp": "cpp", "*.cpp": "cpp" },
  "clangd.arguments": [
    "--compile-commands-dir=${workspaceFolder}/build/debug",
    "--background-index",
    "--clang-tidy",
    "--header-insertion=never"
  ]
}
```

### 0.5 Create `CMakePresets.json` in the project root:

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
  "configurePresets": [
    {
      "name": "base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": {
        "CMAKE_TOOLCHAIN_FILE": "$env{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake",
        "CMAKE_EXPORT_COMPILE_COMMANDS": "ON"
      }
    },
    {
      "name": "debug",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" }
    },
    {
      "name": "release",
      "inherits": "base",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Release" }
    }
  ],
  "buildPresets": [
    { "name": "debug",   "configurePreset": "debug" },
    { "name": "release", "configurePreset": "release" }
  ],
  "testPresets": [
    {
      "name": "debug",
      "configurePreset": "debug",
      "output": { "outputOnFailure": true }
    }
  ]
}
```

### 0.6 Create the directory tree (Section 1)

Use the file system to create every directory listed in Section 1 below. The directories will be empty at this point — populate them as you implement each phase.

### 0.7 Create `vcpkg.json` and the top-level `CMakeLists.txt`

Use the exact content from Sections 2 and 3.1.

### 0.8 Configure and verify

Run from the project root:
```cmd
cmake --preset debug
```
This will trigger vcpkg to download and build all dependencies. **Warn the user that this initial configure takes 15-45 minutes** (Polyscope and Assimp compile from source). Once it succeeds, you're ready to write code.

If configure fails, diagnose and report. Common causes: `VCPKG_ROOT` not visible to the shell (have user re-launch terminal), missing Windows SDK, vcpkg ports outdated (run `git pull` in `C:\dev\vcpkg`).

### 0.9 Initial commit
Once configure succeeds, ask the user for permission, then:
```cmd
git add .gitignore .clang-format .vscode/ CMakePresets.json CMakeLists.txt vcpkg.json CLAUDE.md SolarCookerRayTracer_Plan.md
git commit -m "Bootstrap: project skeleton, build config, vcpkg manifest"
```

Now proceed to Phase 1 from Section 10.

---

## 1. Repository layout (create this exact structure)

```
solar-cooker-rt/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
├── CLAUDE.md
├── SolarCookerRayTracer_Plan.md
├── .clang-format
├── .gitignore
├── .vscode/
│   └── settings.json
├── .github/workflows/ci.yml          (added at end of Phase 1)
├── cmake/
│   └── CompilerWarnings.cmake
├── include/scrt/
│   ├── core/
│   │   ├── Ray.hpp
│   │   ├── Hit.hpp
│   │   ├── AABB.hpp
│   │   ├── Transform.hpp
│   │   └── Units.hpp
│   ├── math/
│   │   ├── Vec.hpp
│   │   ├── Constants.hpp
│   │   ├── Sampling.hpp
│   │   └── Rng.hpp
│   ├── optics/
│   │   ├── Reflect.hpp
│   │   ├── Refract.hpp
│   │   ├── Fresnel.hpp
│   │   └── Paraxial.hpp
│   ├── surfaces/
│   │   ├── Surface.hpp
│   │   ├── Plane.hpp
│   │   ├── Sphere.hpp
│   │   ├── Paraboloid.hpp
│   │   ├── GeneralQuadric.hpp
│   │   ├── ImplicitSDF.hpp
│   │   └── TriangleMesh.hpp
│   ├── materials/
│   │   ├── Material.hpp
│   │   ├── PerfectMirror.hpp
│   │   ├── RealMirror.hpp
│   │   ├── Dielectric.hpp
│   │   └── Absorber.hpp
│   ├── sources/
│   │   ├── SunSource.hpp
│   │   ├── Pillbox.hpp
│   │   └── Buie.hpp
│   ├── scene/
│   │   ├── Scene.hpp
│   │   ├── Element.hpp
│   │   ├── Receiver.hpp
│   │   └── Aperture.hpp
│   ├── accel/
│   │   └── BVH.hpp
│   ├── tracer/
│   │   ├── Tracer.hpp
│   │   └── FluxAccumulator.hpp
│   ├── io/
│   │   ├── SceneLoader.hpp
│   │   ├── MeshImporter.hpp
│   │   └── ResultsExporter.hpp
│   └── viz/
│       ├── Viewer.hpp
│       ├── RayRenderer.hpp
│       └── FluxPlotter.hpp
├── src/                              # one .cpp per non-trivial header
│   └── (mirror of include/scrt where implementation is needed)
├── app/
│   └── main.cpp                      # desktop application entry point
├── tests/
│   ├── test_main.cpp                 # doctest entry point
│   ├── test_vec.cpp
│   ├── test_reflect_refract.cpp
│   ├── test_fresnel.cpp
│   ├── test_intersections.cpp
│   ├── test_paraxial.cpp
│   ├── test_sun.cpp
│   ├── test_parabola_focus.cpp
│   ├── test_thin_lens.cpp
│   └── test_scene_io.cpp
├── examples/
│   ├── parabolic_dish.json
│   ├── fresnel_lens_cooker.json
│   ├── box_cooker.json
│   └── scheffler_reflector.json
└── assets/
    └── meshes/
```

---

## 2. Dependencies — write `vcpkg.json` exactly:

```json
{
  "name": "solar-cooker-rt",
  "version": "0.1.0",
  "dependencies": [
    "glm",
    "nlohmann-json",
    "polyscope",
    "imgui",
    "implot",
    "assimp",
    "doctest",
    "fmt",
    "spdlog"
  ]
}
```

**Notes:**
- **glm** — 3D vectors, matrices. Use `glm::dvec3` (double precision) throughout.
- **nlohmann-json** — scene file parsing.
- **polyscope** — 3D viewer; pulls in ImGui automatically.
- **imgui / implot** — UI panels and 2D plots.
- **assimp** — imports OBJ, STL, glTF, FBX from CAD/Blender.
- **doctest** — unit tests, single-header.
- **fmt / spdlog** — logging and formatted output.

If vcpkg fails to find any package, fall back to CMake's `FetchContent` for that single package and notify the user.

---

## 3. Build configuration

### 3.1 Top-level `CMakeLists.txt` requirements

Create `CMakeLists.txt` with this structure (you may adapt formatting, but all functional requirements must be met):

```cmake
cmake_minimum_required(VERSION 3.25)
project(solar_cooker_rt CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(cmake/CompilerWarnings.cmake)

# Find vcpkg-installed dependencies
find_package(glm CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(doctest CONFIG REQUIRED)
# Polyscope, ImGui, ImPlot, Assimp added in later phases.

# ---------- scrt_core (physics core, no GUI) ----------
add_library(scrt_core STATIC)
target_include_directories(scrt_core PUBLIC include)
target_link_libraries(scrt_core PUBLIC
    glm::glm nlohmann_json::nlohmann_json fmt::fmt spdlog::spdlog)
target_compile_features(scrt_core PUBLIC cxx_std_20)
set_project_warnings(scrt_core)

# Sources will be added per phase via target_sources(scrt_core PRIVATE ...)

# ---------- scrt_tests ----------
enable_testing()
add_executable(scrt_tests)
target_link_libraries(scrt_tests PRIVATE scrt_core doctest::doctest)
set_project_warnings(scrt_tests)

# Test sources added per phase via target_sources(scrt_tests PRIVATE ...)

include(CTest)
include(${doctest_DIR}/doctest.cmake OPTIONAL RESULT_VARIABLE _doctest_cmake)
if(_doctest_cmake)
    doctest_discover_tests(scrt_tests)
else()
    add_test(NAME scrt_tests COMMAND scrt_tests)
endif()

# scrt_viz and scrt_app are added in Phase 6.
```

### 3.2 `cmake/CompilerWarnings.cmake`

```cmake
function(set_project_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion
            -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Wunused
            -Woverloaded-virtual -Wdouble-promotion)
    endif()
    # In CI, treat warnings as errors. Locally: warnings stay warnings.
    if(DEFINED ENV{CI})
        if(MSVC)
            target_compile_options(${target} PRIVATE /WX)
        else()
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
```

### 3.3 CI workflow `.github/workflows/ci.yml` (added at end of Phase 1)

After Phase 1 tests pass, add a GitHub Actions workflow that builds and tests on `windows-latest`, `ubuntu-latest`, and `macos-latest` using CMakePresets. Cache the `vcpkg_installed` directory keyed on hash of `vcpkg.json` to avoid 30-min rebuilds on every push.

---

## 4. Coding conventions — obey these strictly

- **Namespaces:** everything under `namespace scrt`. Submodules use nested namespaces: `scrt::math`, `scrt::core`, `scrt::optics`, `scrt::surfaces`, `scrt::materials`, `scrt::sources`, `scrt::scene`, `scrt::accel`, `scrt::tracer`, `scrt::io`, `scrt::viz`.
- **Precision:** all geometry and physics in `double`. The viz layer may downcast to `float` for GPU buffers.
- **Units:** SI throughout. Lengths in meters, power in watts, angles in radians. Wavelengths in nm only at I/O boundaries; convert to meters internally for physics.
- **Vectors:** `using vec3 = glm::dvec3;` declared once in `math/Vec.hpp`. Do not define a custom vector type.
- **Pointers:** owning via `std::unique_ptr`, non-owning via raw pointers or `std::span`. No `new` / `delete`. No `shared_ptr` unless lifetime genuinely requires it.
- **Interfaces:** abstract bases have `virtual ~T() = default;` and **deleted copy/move**. Subclasses are `final`.
- **Error handling:** `throw std::runtime_error` for user-facing errors (bad scene file, missing mesh); `assert` for programmer errors. No silent failures.
- **Testing:** every non-trivial function in `scrt_core` has at least one doctest case. Aim for >80% line coverage of the core.
- **Doxygen:** one-line summary on every public class and function. Equations in LaTeX-ish ASCII in comments.
- **No RTTI in hot paths.** No `dynamic_cast` in the tracer loop — dispatch through virtual `intersect` only.
- **clang-format:** the `.clang-format` is authoritative; format on save is enabled in `.vscode/settings.json`.

---

## 5. Module-by-module specifications

### 5.1 `math/` — vectors, RNG, sampling

**`math/Constants.hpp`** defines:
- `PI`, `TWO_PI`, `DEG2RAD`, `RAD2DEG`
- `SOLAR_HALF_ANGLE_RAD = 4.65e-3`
- `DNI_STANDARD = 1000.0` (W/m²)
- `C0 = 299792458.0` (m/s)
- `EPSILON_T = 1e-6` (ray self-intersection guard)

**`math/Vec.hpp`:**
```cpp
#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtx/norm.hpp>

namespace scrt::math {
using vec3 = glm::dvec3;
using vec2 = glm::dvec2;
using mat3 = glm::dmat3;
using mat4 = glm::dmat4;

inline vec3 safe_normalize(const vec3& v) {
    double l2 = glm::dot(v, v);
    assert(l2 > 0.0 && "safe_normalize on zero vector");
    return v / std::sqrt(l2);
}
} // namespace scrt::math
```

**`math/Rng.hpp`** — wraps `std::mt19937_64` with thread-local state. Expose:
- `double uniform01()` — [0,1)
- `double uniform(double a, double b)`
- `vec2 unit_disk_concentric()` — Shirley's concentric mapping
- `vec3 unit_sphere_direction()`
- `void seed(uint64_t)` — for deterministic testing

**`math/Sampling.hpp`** — inverse-CDF helpers for sunshapes (called by sources, populated in Phase 7).

### 5.2 `core/` — primitives shared across modules

**`core/Ray.hpp`:**
```cpp
#pragma once
#include "scrt/math/Vec.hpp"
#include <cstdint>

namespace scrt::core {
struct Ray {
    math::vec3 origin {0.0};
    math::vec3 direction {0.0, 0.0, 1.0};   // unit
    double     power {1.0};                  // watts
    double     wavelength_nm {550.0};
    int        bounces {0};
    std::uint32_t id {0};
};
} // namespace scrt::core
```

**`core/Hit.hpp`** — carries `t`, `position`, `normal` (unit, oriented against the incoming ray), `uv` (surface params), and `const surfaces::Surface* surface` (forward-declare the Surface class).

**`core/AABB.hpp`** — axis-aligned bounding box with `expand(vec3)`, `expand(AABB)`, and `intersect(const Ray&, double& t_near, double& t_far)` using the slab method.

**`core/Transform.hpp`** — the **key abstraction enabling free placement of optical elements**. Holds a `dmat4` forward transform, its inverse, and a `dmat3` for normals (inverse-transpose of upper-left 3×3). Public API:

```cpp
class Transform {
public:
    Transform();  // identity
    static Transform from_translation(math::vec3 t);
    static Transform from_rotation_axis_angle(math::vec3 axis, double radians);
    static Transform from_euler_xyz(math::vec3 euler_radians);
    static Transform from_look_at(math::vec3 eye, math::vec3 target, math::vec3 up);
    static Transform from_matrix(const math::mat4& m);

    Transform compose(const Transform& child) const;

    math::vec3 point_to_world(math::vec3 p) const;
    math::vec3 direction_to_world(math::vec3 d) const;
    math::vec3 normal_to_world(math::vec3 n) const;

    math::vec3 point_to_local(math::vec3 p) const;
    math::vec3 direction_to_local(math::vec3 d) const;
    Ray ray_to_local(const Ray& r) const;

    const math::mat4& matrix() const { return m_; }
    const math::mat4& inverse() const { return inv_; }

private:
    math::mat4 m_;
    math::mat4 inv_;
    math::mat3 normal_xform_;
};
```

Implement in `src/core/Transform.cpp`. Every `Surface` derivation defines geometry in a canonical local frame and stores a `Transform`. Intersection: convert ray to local, solve in closed form, transform hit back. This is how the user freely places mirrors and lenses anywhere.

### 5.3 `optics/` — reflection, refraction, Fresnel, paraxial

**`optics/Reflect.hpp`** — pure header function:
```cpp
namespace scrt::optics {
inline math::vec3 reflect(math::vec3 i, math::vec3 n) noexcept {
    return i - 2.0 * glm::dot(i, n) * n;
}
}
```

**`optics/Refract.hpp`** — vector-form Snell's law:
```cpp
namespace scrt::optics {
/// Refract incident direction i across normal n with eta = n1/n2.
/// Assumes n points against the incoming ray (i·n < 0 on entry).
/// Sets tir=true and returns reflect(i,n) on total internal reflection.
math::vec3 refract(math::vec3 i, math::vec3 n, double eta, bool& tir) noexcept;
}
```
Implement in `src/optics/Refract.cpp`.

**`optics/Fresnel.hpp`:**
```cpp
namespace scrt::optics {
struct FresnelResult { double R; double T; };  // R + T = 1

/// Exact unpolarized Fresnel. cos_theta_t computed by caller (or pass refracted dir).
FresnelResult fresnel_unpolarized(double cos_theta_i, double cos_theta_t,
                                  double n1, double n2) noexcept;

/// Schlick's fast approximation; within ~1% for solar work.
double schlick_R0(double n1, double n2) noexcept;
double schlick_R(double cos_theta_i, double R0) noexcept;
}
```

**`optics/Paraxial.hpp`** — 2×2 ABCD matrices for design and validation only:
```cpp
namespace scrt::optics {
using Mat2 = glm::dmat2;

Mat2 abcd_free_space(double d);
Mat2 abcd_thin_lens(double f);
Mat2 abcd_flat_refraction(double n1, double n2);
Mat2 abcd_spherical_refraction(double n1, double n2, double R);
Mat2 abcd_spherical_mirror(double R);

Mat2 cascade(std::span<const Mat2> elements_in_order);

struct ParaxialRay { double y; double theta; };
ParaxialRay apply(const Mat2& M, ParaxialRay in);
}
```

### 5.4 `surfaces/` — abstract Surface and concrete types

**`surfaces/Surface.hpp`:**
```cpp
namespace scrt::surfaces {
class Surface {
public:
    virtual ~Surface() = default;
    Surface(const Surface&) = delete;
    Surface& operator=(const Surface&) = delete;
    Surface(Surface&&) = delete;
    Surface& operator=(Surface&&) = delete;

    /// Intersect ray in WORLD space. Implementation typically transforms
    /// to local, solves analytically, then transforms the hit back.
    virtual bool intersect(const core::Ray& r, double t_min, double t_max,
                           core::Hit& hit) const = 0;

    /// World-space AABB for BVH construction.
    virtual core::AABB world_bounds() const = 0;

    /// Triangulation for the viewer.
    virtual void tessellate(int nseg,
                            std::vector<math::vec3>& verts,
                            std::vector<std::uint32_t>& indices) const = 0;

    void set_transform(core::Transform t) { xform_ = std::move(t); }
    const core::Transform& transform() const { return xform_; }
    void set_material(const materials::Material* m) { material_ = m; }
    const materials::Material* material() const { return material_; }
    void set_name(std::string s) { name_ = std::move(s); }
    const std::string& name() const { return name_; }

protected:
    Surface() = default;
    core::Transform xform_;
    const materials::Material* material_ = nullptr;
    std::string name_;
};
}
```

**Concrete surfaces (implement in this order across phases):**

1. **`Plane`** — bounded rectangle (u-axis, v-axis, half-widths). Use for flat mirrors, box cooker walls, receivers.
2. **`Sphere`** — center + radius + optional angular bounds (spherical cap). Stable quadratic.
3. **`Paraboloid`** — focal length + aperture radius + depth. `x² + y² − 4fz = 0` in local space.
4. **`GeneralQuadric`** — 10 coefficients (A..J) + axis-aligned aperture box. Covers ellipsoid, hyperboloid, cone, cylinder. **First "equation-defined" surface type.**
5. **`ImplicitSDF`** — `std::function<double(vec3)>` in local space + bounding box. Sphere tracing intersection, central-difference normal. **Second "equation-defined" type — handles freeform.**
6. **`TriangleMesh`** — list of triangles (loaded via Assimp). Local-space BVH for intersection. **CAD/Blender import target.**

**Pattern every surface follows:**
1. Transform ray to local via `xform_.ray_to_local(r)`.
2. Solve analytically (or sphere-trace / BVH-traverse).
3. On hit: fill `hit.position`, compute local normal from analytic gradient, transform both back via `xform_.point_to_world` and `xform_.normal_to_world`.
4. Set `hit.surface = this`.

### 5.5 `materials/` — ray-surface interaction

**`materials/Material.hpp`:**
```cpp
namespace scrt::materials {
enum class InteractionKind { Absorbed, Reflected, Refracted, Split };

struct Interaction {
    InteractionKind kind;
    core::Ray reflected;
    core::Ray transmitted;
    bool terminate = false;
};

class Material {
public:
    virtual ~Material() = default;
    Material(const Material&) = delete;
    Material& operator=(const Material&) = delete;

    virtual Interaction interact(const core::Ray& r, const core::Hit& h,
                                 math::Rng& rng) const = 0;
    void set_name(std::string s) { name_ = std::move(s); }
    const std::string& name() const { return name_; }

protected:
    Material() = default;
    std::string name_;
};
}
```

**Concrete materials:**

1. **`PerfectMirror`** — reflectance = 1.0. One reflected ray, unchanged power.
2. **`RealMirror`** — constant reflectance ρ ∈ [0,1] (aluminum ρ ≈ 0.85, silvered glass ρ ≈ 0.92). Uses **energy partitioning** (multiplies power by ρ). Optional `slope_error_mrad` for Gaussian normal perturbation.
3. **`Dielectric`** — n, absorption coefficient α (1/m). Default to **split mode** (spawn both reflected and refracted weighted by Fresnel R and T) for low-variance flux. Russian roulette mode optional.
4. **`Absorber`** — perfect absorber for the receiver. `terminate = true`.

### 5.6 `sources/` — the sun

**`sources/SunSource.hpp`:**
```cpp
namespace scrt::sources {
class SunSource {
public:
    virtual ~SunSource() = default;
    SunSource(const SunSource&) = delete;
    SunSource& operator=(const SunSource&) = delete;

    virtual core::Ray sample_ray(const scene::Aperture& ap, math::Rng& rng) const = 0;

    void set_sun_direction(math::vec3 d) { sun_direction_ = glm::normalize(d); }
    math::vec3 sun_direction() const { return sun_direction_; }
    void set_dni(double dni_wm2) { dni_ = dni_wm2; }
    double dni() const { return dni_; }

protected:
    SunSource() = default;
    math::vec3 sun_direction_ {0.0, 0.0, -1.0};
    double dni_ = 1000.0;
};
}
```

**Concrete sources:**

1. **`Pillbox`** — uniform disk of half-angle 4.65 mrad. Inverse-CDF: `theta = half_angle * sqrt(xi1)`, `phi = 2π·xi2`. **Default for early phases.**
2. **`Buie`** — Buie et al. 2003 sunshape, parameter circumsolar ratio χ ∈ [0.02, 0.10]. Rejection sampling. **Phase 7.**

Primary ray power: `power = DNI * aperture_area / N_rays`.

### 5.7 `scene/` — composition

**`scene/Element.hpp`** — bundles a `Surface` (owned via unique_ptr), display color, user-facing name.

**`scene/Receiver.hpp`** — specialized surface (Plane or Sphere) wrapped with a `FluxAccumulator`. Material is always `Absorber`.

**`scene/Aperture.hpp`** — disk perpendicular to sun vector, sized to enclose all primary optics. Driver samples N_rays origins on this aperture.

**`scene/Scene.hpp`:**
```cpp
class Scene {
public:
    std::size_t add_material(std::unique_ptr<materials::Material> m);
    std::size_t add_surface(std::unique_ptr<surfaces::Surface> s);
    void set_receiver(std::unique_ptr<scene::Receiver> r);
    void set_sun(std::unique_ptr<sources::SunSource> s);
    void set_aperture(scene::Aperture a);

    std::span<const std::unique_ptr<surfaces::Surface>> surfaces() const;
    const scene::Receiver* receiver() const;
    const sources::SunSource* sun() const;
    const scene::Aperture& aperture() const;

    /// World-space closest-hit. Uses BVH if built, else linear scan.
    bool intersect(const core::Ray& r, double t_min, double t_max,
                   core::Hit& hit) const;

    void build_acceleration_structure();
};
```

### 5.8 `accel/` — BVH

Top-down SAH BVH over `Surface*`. Median split on longest centroid-bounds axis, leaf size 4. Opt-in via `Scene::build_acceleration_structure()`. Skip for scenes with ≤20 surfaces.

### 5.9 `tracer/` — Monte Carlo loop and flux accumulator

**`tracer/FluxAccumulator.hpp`:**
```cpp
class FluxAccumulator {
public:
    FluxAccumulator(math::vec3 origin, math::vec3 u_axis, math::vec3 v_axis,
                    double width_m, double height_m, int nx, int ny);

    /// Thread-safe via per-thread local buffers, merged in finalize().
    void deposit(const core::Ray& r, const core::Hit& h) noexcept;

    void finalize(std::size_t total_primary_rays);

    std::vector<double> flux_map_wm2() const;
    std::vector<double> flux_stderr_wm2() const;
    double total_power_w() const;
    double peak_flux_wm2() const;
    double concentration_ratio(double dni_wm2) const;
    int nx() const; int ny() const;
    double bin_width_m() const; double bin_height_m() const;
};
```

**Critical:** never use atomic adds on shared bins. Each thread gets its own `std::vector<double>`; merge at the end. Store first AND second moments for per-bin standard error.

**`tracer/Tracer.hpp`:**
```cpp
struct TraceConfig {
    std::size_t n_primary_rays  = 1'000'000;
    int max_bounces             = 16;
    double power_cutoff_w       = 1e-9;
    std::uint64_t rng_seed      = 0;       // 0 → random_device
    int num_threads             = 0;       // 0 → hardware_concurrency
    bool record_paths           = false;
    std::size_t max_paths_to_record = 2000;
};

struct TraceResult {
    std::size_t primary_rays_traced = 0;
    std::size_t total_hits          = 0;
    double wall_time_s              = 0.0;
    std::vector<std::vector<math::vec3>> sampled_paths;
};

class Tracer {
public:
    explicit Tracer(const scene::Scene& s) : scene_(&s) {}
    TraceResult run(const TraceConfig& cfg, FluxAccumulator& acc) const;

private:
    void trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                   std::vector<math::vec3>* path_out, int max_bounces,
                   double power_cutoff) const;
    const scene::Scene* scene_;
};
```

**Inner loop pseudocode:**
```cpp
void Tracer::trace_one(core::Ray r, FluxAccumulator& acc, math::Rng& rng,
                       std::vector<math::vec3>* path,
                       int max_bounces, double cutoff) const {
    if (path) path->push_back(r.origin);
    for (int b = 0; b < max_bounces; ++b) {
        core::Hit h;
        if (!scene_->intersect(r, EPSILON_T, std::numeric_limits<double>::max(), h))
            return;
        if (path) path->push_back(h.position);

        auto inter = h.surface->material()->interact(r, h, rng);
        switch (inter.kind) {
            case materials::InteractionKind::Absorbed:
                acc.deposit(r, h);
                return;
            case materials::InteractionKind::Reflected:
                r = inter.reflected;  break;
            case materials::InteractionKind::Refracted:
                r = inter.transmitted;  break;
            case materials::InteractionKind::Split:
                trace_one(inter.reflected, acc, rng, path, max_bounces, cutoff);
                r = inter.transmitted;  break;
        }
        if (r.power < cutoff) return;
    }
}
```

Outer driver: `std::for_each(std::execution::par_unseq, ...)` with `thread_local math::Rng` and per-thread flux buffers.

### 5.10 `io/` — scene files and mesh import

**`io/SceneLoader.hpp`** — loads JSON scene. Schema:

```json
{
  "scene": {
    "name": "Parabolic dish demo",
    "sun": {
      "direction": [0, 0, -1],
      "dni_wm2": 1000.0,
      "sunshape": { "type": "pillbox", "half_angle_mrad": 4.65 }
    },
    "aperture": {
      "type": "disk",
      "center": [0, 0, 2.0],
      "normal": [0, 0, 1],
      "radius": 0.5
    },
    "materials": [
      { "id": "aluminum", "type": "real_mirror",
        "reflectance": 0.85, "slope_error_mrad": 2.0 },
      { "id": "glass",    "type": "dielectric",
        "n": 1.52, "absorption_per_m": 4.0 },
      { "id": "black",    "type": "absorber" }
    ],
    "elements": [
      {
        "name": "primary_dish",
        "surface": { "type": "paraboloid",
                     "focal_length_m": 0.6, "aperture_radius_m": 0.5 },
        "transform": { "translation": [0,0,0], "rotation_euler_deg": [0,0,0] },
        "material": "aluminum"
      },
      {
        "name": "custom_cad_housing",
        "surface": { "type": "mesh",
                     "path": "assets/meshes/housing.obj",
                     "units": "meters" },
        "transform": { "translation": [0,0,0.1], "scale": [1,1,1] },
        "material": "aluminum"
      },
      {
        "name": "custom_freeform_lens",
        "surface": {
          "type": "quadric",
          "coeffs": { "A":1, "B":1, "C":0, "D":0, "E":0,
                      "F":0, "G":0, "H":0, "I":-1, "J":0 },
          "aperture_box": { "min":[-0.1,-0.1,-0.01], "max":[0.1,0.1,0.01] }
        },
        "transform": { "translation": [0,0,0.5] },
        "material": "glass"
      }
    ],
    "receiver": {
      "surface": { "type": "plane",
                   "u_axis": [1,0,0], "v_axis": [0,1,0],
                   "half_width": 0.05, "half_height": 0.05 },
      "transform": { "translation": [0,0,0.6] },
      "grid": { "nx": 128, "ny": 128 }
    }
  },
  "trace": {
    "n_primary_rays": 1000000,
    "max_bounces": 8,
    "record_paths": true,
    "max_paths_to_record": 500,
    "rng_seed": 42
  }
}
```

Validate every required field; throw with line number on failure.

**`io/MeshImporter.hpp`** — wraps Assimp. Supports `.obj`, `.stl`, `.ply`, `.glb`, `.gltf`, `.fbx`. Honors `"units"` field (meters/millimeters/inches → scale factor).

```cpp
struct ImportedMesh {
    std::vector<math::vec3> vertices;
    std::vector<std::uint32_t> indices;
};
ImportedMesh import_mesh(const std::filesystem::path& file,
                         double scale_to_meters = 1.0);
```

**`io/ResultsExporter.hpp`:**
- `export_flux_csv(...)` — flux map as CSV.
- `export_flux_npy(...)` — for Python post-processing.
- `export_summary_json(...)` — total power, peak, concentration ratio, MC stderr, wall time.
- `export_scene_obj(...)` — for debugging in Blender.

### 5.11 `viz/` — Polyscope viewer + ImPlot

**`viz/Viewer.hpp`** owns the application window:
```cpp
class Viewer {
public:
    Viewer();
    ~Viewer();
    void set_scene(const scene::Scene* s);
    void set_trace_result(const tracer::TraceResult* r);
    void set_flux(const tracer::FluxAccumulator* acc);
    void run();  // blocks until window closed
};
```

**What the viewer shows:**
1. Every surface registered with Polyscope as a mesh via `tessellate()`. Colored, named.
2. Aperture as translucent disk.
3. Sun direction as an arrow.
4. Sampled ray paths as Polyscope curve networks (only if `record_paths=true`), colored by power (jet colormap).
5. Receiver mesh with vertex colors = flux map values (linear or log selectable).
6. **ImGui side panel** with: scene tree, trace controls (ray-count slider, max-bounces, Run button), sun controls (azimuth/elevation, DNI, sunshape), material inspector (live edit reflectance/n/α), results panel (total power, peak, mean, concentration ratio, MC stderr).
7. **ImPlot window** with two tabs:
   - **Aperture plot:** 2D heatmap of primary entry flux (uniform up to MC noise) + x and y histograms.
   - **Receiver plot:** 2D heatmap with profile slices through peak, numerical readouts (peak W/m², total W, FWHM, concentration ratio, MC stderr per bin), PNG / CSV export.

---

## 6. Application entry point `app/main.cpp`

CLI: `scrt_app scene.json [--headless] [--rays N] [--out results/]`

- Parse args, load scene via `SceneLoader`, build acceleration structure.
- `--headless`: run trace, export results, exit.
- Otherwise: instantiate Viewer, run quick 10k-ray preview, open Polyscope main loop. User clicks Run for the full trace.

---

## 7. Free placement — design requirement

Every `Surface` stores a `core::Transform`. Intersection always in local space. User places elements via JSON (see schema) or programmatically:
```cpp
auto dish = std::make_unique<Paraboloid>(0.6, 0.5);
dish->set_transform(
    Transform::from_translation({0.3, 0.0, 0.1})
    .compose(Transform::from_rotation_axis_angle({0,1,0}, 15.0 * DEG2RAD)));
```
The tracer never special-cases placement. Non-uniform scale is supported via the inverse-transpose normal in `Transform::normal_to_world`.

---

## 8. CAD / Blender import

User workflow:
1. Design cooker in Blender / SolidWorks / Fusion 360.
2. Export as `.obj` or `.glb` with **meters** as units.
3. Reference in scene JSON: `"surface": { "type": "mesh", "path": "...", "units": "meters" }`.
4. Assign material ID.
5. Run simulator — imported mesh participates like any analytic surface.

In the README, advise: analytic primitives (paraboloid, sphere) give exact optical behavior; meshes introduce sub-triangle artifacts. Use analytic surfaces for primary optics, meshes for housings and complex receivers.

---

## 9. Validation tests T1-T12

These are **gatekeepers** for each phase. Implement them in `tests/`. After each phase, run `ctest` and report results before proposing the next phase.

| ID | Test | Phase |
|---|---|---|
| T1 | Snell at flat air-glass interface, 30° → refracted angle = arcsin(sin30°/1.5) within 1e-9 | 1 |
| T2 | Fresnel R at normal air-glass = 0.04 within 1e-9 | 1 |
| T3 | TIR glass-air at 45° (critical 41.8°) → tir=true, returned dir = reflect(i,n) exactly | 1 |
| T4 | `\|reflect(i,n)\|` = 1, `reflect(reflect(i,n),n) = i`, angles match | 1 |
| T5 | Ray-sphere known intersection at predicted t-values | 2 |
| T6 | Parallel pencil onto paraboloid f=0.5 → all reflected rays through (0,0,0.5) within 1e-8 m | 2 |
| T7 | Compose rotation + translation, point round-trip identity within 1e-12 | 1 |
| T8 | Thin lens f=0.1, object s=0.2 → image at s'=0.2 by both ABCD and 3D marginal trace at 0.1° | 3 |
| T9 | Dielectric split: sum of reflected + transmitted power = incoming, 0-89° angles, within 1e-12 | 3 |
| T10 | Paraboloid f=1m + pillbox sun → focal-spot D90 = 2·f·tan(4.65 mrad) within 5%, 10⁶ rays | 2 |
| T11 | Scene JSON round-trip byte-for-byte after canonical formatting | 4 |
| T12 | 1m cube exported in mm with `"units":"millimeters"` → world bounds = 0.001m cube | 5 |

---

## 10. Phased build plan — execute in order

After each phase:
1. Run `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`.
2. Report the test results to the user.
3. Update the "Current phase" line in `CLAUDE.md`.
4. Commit with a descriptive message.
5. Wait for user approval before starting the next phase.

### Phase 1 — Bootstrap (~1-2 days)
**Files:** `math/Vec.hpp`, `math/Constants.hpp`, `math/Rng.hpp`+cpp, `core/Ray.hpp`, `core/Hit.hpp`, `core/AABB.hpp`+cpp, `core/Transform.hpp`+cpp, `optics/Reflect.hpp`, `optics/Refract.hpp`+cpp, `optics/Fresnel.hpp`+cpp.
**Tests:** T1, T2, T3, T4, T7 + a sanity test for Rng.
**Acceptance:** all 5+1 tests pass on Windows MSVC.
**Then:** add `.github/workflows/ci.yml` for cross-platform CI; commit.

### Phase 2 — Analytic surfaces + first flat-geometry trace (~3-4 days)
**Files:** `surfaces/Surface.hpp`, `surfaces/Plane`+cpp, `surfaces/Sphere`+cpp, `surfaces/Paraboloid`+cpp, `materials/PerfectMirror`, `materials/Absorber`, `sources/Pillbox`+cpp, `scene/Aperture`+cpp, `scene/Receiver`+cpp, `scene/Scene`+cpp (linear scan), `tracer/FluxAccumulator`+cpp, `tracer/Tracer`+cpp (single-threaded).
**Tests:** T5, T6, T10.
**Acceptance:** hard-coded parabolic-dish-plus-receiver scene produces a focal spot. Print as ASCII heatmap in a headless test.

### Phase 3 — Refractive elements + Fresnel (~2-3 days)
**Files:** `materials/Dielectric`+cpp (split-mode + Beer-Lambert), `materials/RealMirror`+cpp (reflectance + slope error), `optics/Paraxial`+cpp.
**Tests:** T8, T9.
**Acceptance:** thin-lens-plus-receiver scene focuses sunlight. Energy conservation holds.

### Phase 4 — Equation-defined surfaces + scene I/O (~3-4 days)
**Files:** `surfaces/GeneralQuadric`+cpp, `surfaces/ImplicitSDF`+cpp, `io/SceneLoader`+cpp, four `examples/*.json`, `io/ResultsExporter`+cpp.
**Tests:** T11.
**Acceptance:** every example scene loads and traces, producing a flux CSV with a stable checksum.

### Phase 5 — Parallelism, BVH, mesh import (~3-4 days)
**Files:** parallelize `Tracer::run` with `std::execution::par_unseq` + per-thread RNG and flux buffers; `accel/BVH`+cpp; `io/MeshImporter`+cpp; `surfaces/TriangleMesh`+cpp with local BVH.
**Tests:** T12.
**Acceptance:** 100k-tri mesh scene traces 10⁶ rays in <5 s on 8 cores. Mesh and analytic versions of the same scene match within MC noise.

### Phase 6 — Visualization + plots (~4-5 days)
**Files:** `viz/Viewer`+cpp, `viz/RayRenderer`+cpp, `viz/FluxPlotter`+cpp, `app/main.cpp`. Add `find_package(polyscope CONFIG REQUIRED)`, `imgui`, `implot`, `assimp` to root CMakeLists.txt and create the `scrt_viz` and `scrt_app` targets.
**Acceptance:** `scrt_app examples/parabolic_dish.json` shows the dish, sampled ray paths, flux heatmap on receiver, and a 2D plot tab with numerical readouts. Live material editing triggers re-trace. Capture screenshot for README.

### Phase 7 — Buie sun, spectral hooks, documentation (~2-3 days)
**Files:** `sources/Buie`+cpp, optional Sellmeier coefficients in `Dielectric`, comprehensive `README.md`, Doxygen config.
**Acceptance:** all four example scenes produce flux maps + numerical summaries + screenshots in the README.

---

## 11. Stretch goals (DO NOT IMPLEMENT in v1)

Documented for future work; out of scope:
1. Inverse design via CMA-ES.
2. Spectral ray tracing across AM1.5.
3. Thermal coupling to a 1D pot model.
4. CUDA/Vulkan compute GPU acceleration.
5. Differentiable ray tracing.
6. Polarization (Jones vectors).

---

## 12. Definition of "done"

A user can:
- Open `scrt_app examples/parabolic_dish.json` and see the dish, focused rays, glowing receiver flux map, and a 2D plot with numbers — all in one window.
- Click an element, edit reflectance, see the flux update.
- Swap the analytic paraboloid for a Blender-exported STL and repeat the analysis.
- Press Export → CSV flux map + JSON summary + screenshot.
- Trust the numbers: tests T1-T12 are green and T10 (focal spot size) reproduces the textbook analytical result.

---

# HUMAN: HOW TO LAUNCH

After completing the PREREQUISITES section at the top of this file, paste this exact prompt into the Claude Code chat (with **Plan Mode ON**):

---

> Read `SolarCookerRayTracer_Plan.md` end-to-end before doing anything. That file is the complete brief for this project, including a bootstrap section (Section 0) and seven build phases (Section 10).
>
> Execute Section 0 first (Bootstrap tasks 0.1 through 0.9), then continue into Phase 1 from Section 10. After every phase, run `cmake --build build/debug && ctest --test-dir build/debug --output-on-failure`, report results, update the "Current phase" marker in `CLAUDE.md`, and **wait for my approval** before starting the next phase.
>
> Coding conventions in Section 4 are non-negotiable: `glm::dvec3` everywhere, SI units, no `shared_ptr`, abstract bases with deleted copy/move and virtual destructors.
>
> Platform: Windows, MSVC (current stable release, x64), Ninja generator, vcpkg manifest mode at `C:\dev\vcpkg`. Use `cmake --preset debug` for configuration once `CMakePresets.json` exists.
>
> Before creating any files, output a checklist of the bootstrap tasks (0.1-0.9) and wait for my approval. Then create files in small batches, showing diffs and pausing for review. When asking me to run shell commands like `cmake --preset debug`, warn me that the first configure takes 15-45 minutes due to vcpkg compiling Polyscope, Assimp, and ImGui from source.
>
> Begin.

---

That's it. Hand this single document to Claude Code and get out of the way.
