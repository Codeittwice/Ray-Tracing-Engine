# Solar Cooker Ray Tracer

A physically-based Monte Carlo ray tracer for solar concentrator design, built in C++20 with an interactive Polyscope/ImPlot GUI and a headless CLI mode for batch analysis.

---

## Features

- **Analytic surfaces**: paraboloid, sphere, plane, general quadric
- **Mesh surfaces**: triangulated geometry loaded via Assimp (STL, OBJ, etc.)
- **BVH acceleration**: top-down SAH BVH for scenes with many surfaces
- **Materials**: perfect mirror, real mirror (reflectance + Gaussian slope error), dielectric (Fresnel split + Beer-Lambert absorption, optional Sellmeier wavelength-dependent index), absorber
- **Sun models**: pillbox (uniform disk), Buie 2003 (realistic aureole, parameterised by circumsolar ratio χ)
- **Parallel tracing**: `std::execution::par` with per-thread flux accumulators
- **Export**: CSV flux map, NumPy `.npy`, JSON summary, Wavefront OBJ scene
- **GUI**: Polyscope 3D view + ImPlot flux analysis window, live material editing

---

## Prerequisites

| Tool | Version |
|------|---------|
| Visual Studio (MSVC) | 2025 (v18) or later |
| CMake | 4.x |
| Ninja | any |
| vcpkg | manifest mode at `C:\dev\vcpkg` |

vcpkg packages (declared in `vcpkg.json`): `glm`, `nlohmann-json`, `fmt`, `spdlog`, `doctest`, `assimp`.  
Polyscope and ImPlot are fetched via CMake `FetchContent` at configure time (first configure takes 15–45 min to compile them).

---

## Build

```sh
# Configure (first run downloads and compiles Polyscope, Assimp, ImGui from source)
cmake --preset debug

# Build
cmake --build --preset debug --parallel

# Run tests
ctest --test-dir build/debug --output-on-failure
```

A `release` preset is also available (`cmake --preset release && cmake --build --preset release --parallel`).

`CMakePresets.json` encodes the MSVC/Windows SDK environment so the commands work from any shell without a Developer Command Prompt.

---

## Running

### Interactive GUI

```sh
# Must be launched from a real Windows terminal (not via a subprocess without a desktop context)
build\debug\scrt_app.exe examples\parabolic_dish.json
```

The window shows the 3D scene, sampled ray paths, a heatmap on the receiver, and a **Flux Analysis** panel with 1-D line plots and numerical readouts. Material parameters (reflectance, slope error, refractive index) can be edited live; clicking **Preview** or **Full Trace** re-traces with the new values.

### Headless mode

```sh
build\debug\scrt_app.exe examples\parabolic_dish.json --headless --rays 500000 --out results\
```

Writes `flux.csv` and `summary.json` to `results\` and prints a summary to stdout. No display required.

---

## Scene format

Scenes are JSON files. Top-level keys: `scene` and `trace`.

```jsonc
{
  "scene": {
    "name": "My cooker",
    "aperture": { "center": [0,0,2], "normal": [0,0,1], "radius": 0.5 },
    "sun": {
      "direction": [0, 0, -1],
      "dni_wm2": 1000.0,
      "sunshape": {
        "type": "pillbox",        // or "buie"
        "half_angle_mrad": 4.65  // pillbox only
        // "chi": 0.05           // buie only; circumsolar ratio ∈ [0.02, 0.10]
      }
    },
    "materials": [
      { "id": "m1", "type": "perfect_mirror" },
      { "id": "m2", "type": "real_mirror",   "reflectance": 0.85, "slope_error_mrad": 2.0 },
      { "id": "m3", "type": "dielectric",    "n": 1.5, "absorption_per_m": 0.0,
        "sellmeier": "bk7" },   // optional: "bk7" or "fused_silica"
      { "id": "m4", "type": "absorber" }
    ],
    "elements": [
      {
        "name": "dish",
        "material": "m1",
        "surface": { "type": "paraboloid", "focal_length_m": 0.6, "aperture_radius_m": 0.5 },
        "transform": { "translation": [0,0,0], "rotation_euler_deg": [0,0,0] }
      }
    ],
    "receiver": {
      "surface": { "type": "plane", "half_width": 0.05, "half_height": 0.05 },
      "grid": { "nx": 64, "ny": 64 },
      "transform": { "translation": [0, 0, 0.6] }
    }
  },
  "trace": {
    "n_primary_rays": 100000,
    "max_bounces": 8,
    "rng_seed": 42
  }
}
```

Surface types: `paraboloid`, `sphere`, `plane`, `quadric`, `mesh`.  
For `mesh`, provide `"path": "assets/meshes/file.stl"` (relative to the JSON file) and optionally `"scale_to_meters": 0.001` for millimetre meshes.

---

## Example scenes

All results below use `--rays 500000`, DNI = 1000 W/m², debug build.

### Parabolic dish (`examples/parabolic_dish.json`)

Ø 1 m paraboloid, f = 600 mm, aluminium mirror (ρ = 0.85, σ = 2 mrad slope error).
Receiver: 100 × 100 mm plane at focus.

```
Total power    : 664.2 W
Peak flux      : 9.09 MW/m²
Concentration  : 9087×
Wall time      : 0.72 s
```

### Box cooker (`examples/box_cooker.json`)

V-groove two-mirror concentrator, two 500 × 500 mm flat panels (ρ = 0.90, σ = 1.5 mrad).
Receiver: 200 × 200 mm plane in the groove.

```
Total power    : 39.9 W
Peak flux      : 1.52 kW/m²
Concentration  : 1.52×
Wall time      : 1.42 s
```

### Fresnel lens approximation (`examples/fresnel_lens_cooker.json`)

Ø 240 mm glass sphere (n = 1.5, α = 2 m⁻¹) acting as a thick lens.
Receiver: 120 × 120 mm plane at z = −0.45 m.

```
Total power    : 5.65 W
Peak flux      : 742 W/m²
Concentration  : 0.74×
Wall time      : 1.50 s
```

### Scheffler off-axis reflector (`examples/scheffler_reflector.json`)

Off-axis quadric (ellipsoid section, 20° tilt), silvered (ρ = 0.92, σ = 1 mrad).
Receiver: 100 × 100 mm plane offset laterally from the optical axis.

```
Total power    : 9.77 W
Peak flux      : 4.17 kW/m²
Concentration  : 4.17×
Wall time      : 1.38 s
```

---

## Validation tests

```sh
ctest --test-dir build/debug --output-on-failure
```

| Test | Description |
|------|-------------|
| T1–T3 | GLM `dvec3` precision, `safe_normalize`, orthonormal frame |
| T4 | Snell's law, TIR, `refract()` accuracy |
| T5 | Paraboloid focus: reflected rays converge within 0.1 mm |
| T6 | Perfect-mirror flux on-axis: energy conservation < 1% |
| T7 | Fresnel unpolarised at normal and Brewster angle |
| T8 | BVH hit = brute-force hit for 200 random scenes |
| T9 | Dielectric split: R + T = 1 for 0–89° incidence (within 1e-12) |
| T10 | Focal spot size matches analytic formula within 5% |
| T11 | All four example scenes load, trace, and export without error |
| T12 | JSON round-trip: serialise → parse → serialise is byte-identical |

---

## Project structure

```
include/scrt/
  math/        Vec.hpp, Rng.hpp, Sampling.hpp (TabulatedDist1D)
  core/        Ray.hpp, Hit.hpp, Transform.hpp, AABB.hpp
  optics/      Reflect.hpp, Refract.hpp, Fresnel.hpp, Paraxial.hpp
  materials/   Material.hpp, PerfectMirror, RealMirror, Dielectric, Absorber
  sources/     SunSource.hpp, Pillbox.hpp, Buie.hpp
  surfaces/    Surface.hpp, Plane, Sphere, Paraboloid, GeneralQuadric,
               ImplicitSDF, TriangleMesh
  scene/       Scene.hpp, Aperture.hpp, Receiver.hpp
  accel/       BVH.hpp
  tracer/      Tracer.hpp, FluxAccumulator.hpp
  io/          SceneLoader.hpp, ResultsExporter.hpp, MeshImporter.hpp
  viz/         Viewer.hpp, RayRenderer.hpp, FluxPlotter.hpp
src/           Implementation files mirroring include/ layout
app/main.cpp   CLI entry point (GUI + headless modes)
examples/      Four ready-to-run JSON scenes
tests/         doctest test suite (T1–T12)
```

---

## Notes on geometry modelling

Use **analytic primitives** (paraboloid, sphere, quadric) for primary optics — they give exact surface normals and avoid the sub-triangle artefacts that come from triangulated approximations. Use **mesh surfaces** for housings, complex receivers, or geometry exported from CAD tools where optical precision at the meshing scale is acceptable.

---

## Phase 7 additions

- **`sources/Buie`** — implements the Buie et al. (2003) solar radiance profile. Parameterised by circumsolar ratio χ ∈ [0.01, 0.15] (clear day ≈ 0.05; hazy ≈ 0.10). Uses a 4096-bin tabulated inverse-CDF sampler (header-only `math/Sampling.hpp`). Select in JSON with `"type": "buie", "chi": 0.05`.

- **Sellmeier dispersion in `Dielectric`** — call `di->set_sellmeier(SellmeierCoeffs::bk7())` (C++) or set `"sellmeier": "bk7"` (JSON) for wavelength-dependent refractive index n(λ). Presets: `bk7` (N-BK7 borosilicate) and `fused_silica`. The Sellmeier equation n²(λ) = 1 + Σ Bᵢλ²/(λ²−Cᵢ) is evaluated at each ray's `wavelength_nm` field; without Sellmeier the fixed `n` value is used.
