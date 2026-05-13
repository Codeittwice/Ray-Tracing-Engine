# Solar Cooker RT Codebase Notes

Studied on 2026-05-01. These notes summarize the repository as it exists now, with an emphasis on architecture, data flow, implementation boundaries, tests, and risks to keep in mind before changing code.

## One-Sentence Shape

This is a C++20 Monte Carlo optical ray tracer for solar cooker concentrators, with a clean split between a mostly headless physics core (`scrt_core`), CLI/batch entry points, and a Polyscope/ImGui/ImPlot visualization layer.

## Repository Map

- `include/scrt/` is the public API, mirrored by `src/`.
- `src/math`, `src/core`, `src/optics` provide the numeric foundation: GLM double vectors, RNG, transforms, AABBs, reflection/refraction, Fresnel, paraxial optics.
- `src/surfaces` contains optical geometry: `Plane`, `Sphere`, `Paraboloid`, `CylindricalParaboloid`, `GeneralQuadric`, `ImplicitSDF`, `TriangleMesh`, and `FresnelZoneLens`.
- `src/materials` contains material interactions: perfect mirror, real mirror, dielectric, absorber.
- `src/sources` contains sun models: `Pillbox` and `Buie`.
- `src/scene` owns the scene graph, receiver, aperture, and BVH.
- `src/tracer` traces primary rays and accumulates receiver flux.
- `src/io` parses scene JSON, imports meshes with Assimp, and exports CSV/NPY/JSON/OBJ.
- `src/viz` is the interactive GUI and plotting layer.
- `app/main.cpp` is the interactive/headless launcher.
- `app/compare_main.cpp` is a multi-scene batch comparison CLI.
- `tests/` is a doctest suite built into one `scrt_tests` executable.
- `examples/` has six JSON scenes: dish, trough, box cooker, panel cooker, Fresnel lens, Scheffler reflector.
- `docs/technical_report.tex` is a substantial technical reference, but it appears to include some encoding mojibake in comments/text copied from other files.

## Build Targets And Dependencies

- `scrt_core`: static library for physics, geometry, tracing, IO, and acceleration. No GUI dependency.
- `scrt_tests`: doctest executable linked against `scrt_core`.
- `scrt_compare`: headless multi-scene comparison executable.
- `scrt_viz`: static GUI layer linked against `scrt_core`, Polyscope, and ImPlot.
- `scrt_app`: main desktop/headless executable.

Dependencies come from vcpkg for `glm`, `nlohmann-json`, `fmt`, `spdlog`, `doctest`, and `assimp`. Polyscope and ImPlot are fetched by CMake `FetchContent`. CMake also prepends Polyscope's bundled GLM include path for Polyscope because vcpkg GLM 1.0 removed APIs Polyscope still uses.

`CMakePresets.json` is Windows/MSVC-specific and encodes paths for VS 18 / MSVC 14.50 and Windows SDK 10.0.26100.0. Presets include `debug`, `release`, and `release-dist`; the release-dist preset targets `x64-windows-static` and static MSVC runtime.

## Core Conventions

- Everything lives under `namespace scrt`, with sub-namespaces by module.
- Geometry and physics use `double`, primarily `glm::dvec3` via `scrt::math::vec3`.
- Units are SI: meters, watts, radians. Wavelength is stored as nm on rays for spectral/dielectric features.
- Surfaces are usually defined in local canonical coordinates and placed via `core::Transform`.
- Ownership is mostly `std::unique_ptr`; surfaces hold raw non-owning `const Material*`.
- Surface intersection returns a `core::Hit` with world position, oriented normal, `uv`, `front_face`, and a back-pointer to the surface.
- Material interaction returns one of `Absorbed`, `Reflected`, `Refracted`, or `Split`.

## Simulation Data Flow

1. `io::load_scene(path)` parses JSON into materials, sun source, aperture, elements/surfaces, receiver, and trace config.
2. The loader assigns material raw pointers to surfaces, adds a dedicated absorber material to the receiver, and calls `Scene::build_acceleration_structure()`.
3. `Tracer::run(cfg, acc)` computes per-primary-ray power as `sun.dni() * aperture.area() / n_primary_rays`.
4. Rays are split across slots using `std::execution::par`, one `FluxAccumulator` per slot.
5. Each ray is sampled from `SunSource::sample_ray(aperture, rng)`, assigned power and ID, then bounced through `trace_one`.
6. `Scene::intersect` finds the closest optical surface via BVH or linear scan, then checks the receiver as a separate closest candidate.
7. If a ray hits an absorber, `FluxAccumulator::deposit` bins `ray.power` using `hit.uv` on the receiver plane.
8. Slot accumulators are merged, then `finalize()` converts power/bin to W/m2 using bin area.
9. Results can be viewed in the GUI or exported as CSV, NPY, JSON summary, or OBJ scene geometry.

## Geometry Layer

The analytic surfaces are straightforward and generally solve their exact intersection equations in local space:

- `Plane`: local `z=0` rectangle; `uv` is `(x,y)`. This is central because receiver deposition assumes plane-style `uv`.
- `Sphere`: quadratic intersection against origin-centered local sphere.
- `Paraboloid`: solves `x^2 + y^2 - 4fz = 0`, clipped by aperture radius and `z >= 0`.
- `CylindricalParaboloid`: solves `x^2 - 4fz = 0`, with finite x/y aperture.
- `GeneralQuadric`: 10-coefficient implicit quadratic clipped by local AABB.
- `FresnelZoneLens`: flat annular lens at local `z=0`; per-zone facet normals are precomputed to refract normally incident rays toward `(0,0,-focal_length)`.
- `ImplicitSDF`: sphere-marches within a local AABB using an SDF callback, but tessellation is currently a stub.
- `TriangleMesh`: builds a per-mesh BVH and uses Moller-Trumbore triangle tests.

Important mesh caveat: `TriangleMesh` currently stores vertices and bounds directly in the coordinate system produced by import. It does not use `Surface::xform_` inside `intersect`, `world_bounds`, or `tessellate`. Scene JSON can assign a transform to mesh surfaces, but this transform will not affect imported meshes unless the vertices were already transformed before construction. Analytic surfaces do honor transforms.

## Materials And Optics

- `PerfectMirror` preserves ray power and reflects direction about the hit normal.
- `RealMirror` reflects with power multiplied by `rho`; it optionally perturbs the surface normal by Gaussian slope error in mrad.
- `Absorber` marks the ray as absorbed. The tracer deposits the incoming ray power into the receiver accumulator on any absorbed interaction.
- `Dielectric` uses split mode: it spawns reflected and transmitted rays weighted by exact unpolarized Fresnel `R/T`.
- Dielectric total internal reflection falls back to reflected-only.
- Dielectric absorption is applied with Beer-Lambert only on exit hits (`!h.front_face`), multiplying by `exp(-alpha * h.t)`.
- Sellmeier presets exist for BK7 and fused silica. Absorption can also be a piecewise-linear wavelength spectrum.

Watch point: for transformed surfaces with non-uniform scale, local ray direction is not normalized in `Transform::ray_to_local`. Intersections use local-space `t` and compare it to world-space-ish bounds; this is fine for rigid transforms, but can be subtle or wrong under non-uniform scale. There is a test for normal transforms under non-uniform scale, but not broad intersection tests under scale.

## Sun Models

- `Pillbox` samples a uniform disk on the aperture and a uniform solid-angle cone around `sun_direction_`.
- `Buie` samples a tabulated inverse CDF over the Buie 2003 circumsolar profile from `theta=0` to `43.6 mrad`.
- Both sources leave `ray.power=1`; the tracer overwrites primary power using DNI and aperture area.

## Acceleration

Scene-level BVH:

- `Scene::build_acceleration_structure()` builds over `surfaces_`.
- BVH stores raw `Surface*` and median-splits on the longest centroid-bounds axis.
- Leaf size is `<= 4`.
- The implementation calls this SAH-ish in docs, but the code is median split, not a full surface-area heuristic split.
- The receiver is not part of the BVH; it is tested after surface traversal with the current best `t`.

Triangle mesh BVH:

- Same basic median-split idea over triangle centroids.
- Mesh bounds are computed once from raw vertices.
- No transform integration, as noted above.

## Tracer Details

- `Tracer::trace_one` is recursive only for reflected branch of dielectric splits; it continues iteratively with transmitted branch.
- `max_bounces` applies per recursive call, not as a global path depth budget across split recursion. A reflected split branch can get up to `max_bounces` more bounces from its recursive call.
- Path recording is mutex-limited by `max_paths_to_record`. The code reserves a path slot under lock but does not increment the count until after tracing, so parallel workers can do extra path recording work that is later discarded.
- Hit counting is per slot, merged under the same mutex used for paths.
- Per-slot accumulators avoid atomic adds on the flux grid.
- `FluxAccumulator` currently stores only power sums and final flux. The original plan mentions second moments / standard error, but this code does not implement per-bin variance.

## IO And Scene Format

`SceneLoader` supports:

- Materials: `perfect_mirror`, `real_mirror`, `dielectric`, `absorber`.
- Sun shapes: `pillbox`, `buie`.
- Surfaces: `plane`, `sphere`, `paraboloid`, `quadric`, `fresnel_zone_lens`, `cylindrical_paraboloid`, `mesh`.
- Transform: `rotation_euler_deg` plus `translation`. There is no JSON scale support in the current parser, despite the original plan mentioning scale.
- Mesh scaling: only `scale_to_meters` is read. The older plan/docs mention `units`, but the loader does not implement `units`.
- Receiver: always constructed as a plane receiver via `Receiver(hw,hh,nx,ny)` even if the JSON surface type says `"plane"`; other receiver surface types are not parsed.

`ResultsExporter` supports:

- Flux CSV: row-major grid of W/m2 values.
- NPY: NumPy 1.0 little-endian float64 array with shape `(ny,nx)`.
- Summary JSON: total power, peak flux, concentration, ray count, hits, wall time, grid metadata.
- OBJ: tessellates surfaces and receiver.

Possible NPY watch point: `export_flux_npy` uses a `preamble_fixed` value of 12 while the actual NumPy v1 preamble is 10 bytes before the header content. This should be verified with Python `numpy.load` before relying on NPY export.

## GUI Layer

`Viewer` owns interactive state:

- Loads scenes from the examples directory and runs a 10k preview.
- Registers surface tessellations, receiver flux mesh, aperture, and sampled ray paths in Polyscope.
- Provides ImGui panels for scene browsing, per-surface transforms, material parameters, DNI, trace controls, and results.
- Uses `dynamic_cast` in GUI-only material editing, which is acceptable outside the hot tracer path.
- Rebuilds the scene BVH lazily on next trace after transform changes.

Potential GUI issue: changing transforms re-registers a Polyscope surface mesh with the same name, but it is not obvious whether stale meshes are removed first. Worth checking visually if transform editing leaves duplicates or stale data.

`FluxPlotter` draws heatmap and x/y profiles and has CSV/JSON export buttons. Its JSON export hardcodes DNI as `1000.0`, so exported GUI summaries may be wrong if the scene DNI slider has changed or the scene uses another DNI.

## Tests And Current Verification

I ran:

```powershell
ctest --test-dir build/debug --output-on-failure
```

Result:

```text
100% tests passed, 0 tests failed out of 1
scrt_tests passed in 4.21 sec
```

Test coverage currently includes:

- Vector/RNG/sampling basics.
- Reflection, refraction, TIR, Fresnel, Schlick.
- Transform round-trips and normal transform under non-uniform scale.
- AABB intersections.
- Sphere, paraboloid, cylindrical paraboloid, Fresnel zone lens behavior.
- Pillbox sun sampling and paraboloid focusing.
- Paraxial ABCD checks.
- Dielectric split energy conservation and spectral absorption interpolation.
- Scene JSON canonical round-trip using nlohmann JSON.
- Example scene loading/tracing smoke tests.
- Results exporter smoke tests.
- Mesh importer scale-to-meters bounds check.
- Compare-pipeline CSV smoke tests.

Gaps in test coverage to keep in mind:

- No strong tests for mesh transforms.
- No full validation of NPY loading.
- No tests for GUI behavior.
- No per-bin Monte Carlo standard error because the accumulator does not implement second moments.
- No stress test of BVH correctness against brute force in the current test files, despite the README saying one exists.
- No clear test for non-uniformly scaled analytic surface intersections.
- No test that GUI summary/export uses scene DNI correctly.

## Documentation State

- `README.md` is usable and documents build/run/scene examples, but it contains visible mojibake for several symbols.
- `docs/technical_report.tex` is comprehensive and broadly matches the architecture, but it also contains mojibake and a few claims that look ahead of the current implementation.
- The original plan file is useful historical context but not perfectly synchronized with the current code. Examples: planned `units` mesh field vs implemented `scale_to_meters`, planned variance/stderr vs accumulator's current power-only grid, and planned SAH BVH vs implemented median split.

## High-Value Risks / Cleanup Targets

1. Mesh transform support is the biggest functional mismatch. If users place imported CAD geometry with JSON transforms, the current `TriangleMesh` code appears to ignore those transforms.
2. `FluxAccumulator` docs/comments mention normalization and concentration area factors that the implementation does not use; comments should be corrected or implementation expanded.
3. GUI JSON export hardcodes DNI to 1000.
4. Scene loader and docs disagree on mesh units/scale and receiver capabilities.
5. NPY export header padding should be verified against NumPy.
6. The `technical_report.tex` and README encoding should be cleaned up; this affects trust in docs even when the code is fine.
7. `ImplicitSDF::tessellate` is intentionally empty, so SDF surfaces will not appear in OBJ/export/GUI visualization.
8. Parallel path recording can oversubscribe temporary path work because workers only reserve by reading current count.

## Good Things To Preserve

- The module boundaries are sane: core physics does not depend on GUI.
- Analytic surface implementations are compact and testable.
- Material dispatch is clean and keeps physics outside the tracer loop.
- Per-slot accumulators are a good choice for parallel flux deposition.
- The scene JSON loader is straightforward and throws explicit user-facing errors.
- The example scenes make the project easy to smoke test.
- The GUI uses the same scene/tracer/export pathways as headless mode, which reduces duplicated behavior.

