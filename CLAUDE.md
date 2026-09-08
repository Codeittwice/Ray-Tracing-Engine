# Project: Solar Cooker Ray Tracer (C++ / CMake / vcpkg)

## Source of truth
Follow `docs/plans/SolarCookerRayTracer_Plan.md` exactly. That document specifies:
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
Research tool: Phases 1-7 complete.

App track (`feat/interactive-app`) — turning the research tool into a shippable interactive app.
Scope: runtime model import, free placement with a gizmo, axis lock/centering, object scaling, a real
object outliner with selection highlight, sun off zenith (azimuth/elevation), scene save, and an
AI scene-generation helper. Optical results only.

- [x] Wave 0 — Foundation & safety net (BVH empty-build fix, regression corpus, golden-flux baseline)
- [x] Wave 1 — Sun & aperture physics ∥ Scale & surface geometry
- [ ] Wave 2 — SceneDocument & JSON writer (the pivot everything downstream hangs off)
- [ ] Wave 3 — SceneEditor & mutation ∥ Viewer decomposition + outliner
- [ ] Wave 4 — ImGuizmo & transform UI ∥ model import & units
- [ ] Wave 5 — Async trace, save UI, polish, packaging
- [ ] Wave 6 — AI helper

Out of scope on this track: thermal model, lat/lon geographic sun, TMY weather, day-integrated Wh.

### Open findings from Audit A1 (deferred, none blocking)
Wire into Wave 2 (W3) unless noted:
- `Aperture::mode` and `margin`, `SunAngles`, `auto_fit`/`covers`, `Scene::world_bounds()`,
  `ScaleSupport` and the parameter setters have **no production call site** - referenced only from
  tests. `mode` is a public field that currently does nothing. Wire them into SceneLoader/Tracer,
  or drop the field until a consumer exists.
- A below-horizon sun (`elevation < 0`) is accepted silently: a fixed +Z aperture yields 0 W with
  no diagnostic, and `auto_fit` happily lights the cooker from underground. Reject or warn.
- `orthonormal_frame`'s "byte-identical" comment is **false** - it re-normalizes an already-unit
  vector, which differs in the last bit for ~15% of directions. Harmless today (all 94 scenes have
  direction exactly [0,0,-1], where the frames agree bitwise) but the comment must be corrected.
- A fourth un-collapsed copy of the same heuristic remains in `src/materials/RealMirror.cpp:17-20`.
- `Paraboloid`/`CylindricalParaboloid` `local_bounds()` return an inverted AABB for negative focal
  length (`depth = r^2/4f < 0`). `world_bounds()` still sorts via `expand()`, so nothing breaks
  today, but `local_bounds()` is now public API and `AABB` has no normalizing constructor.
- New parameter setters change `local_bounds()` with no BVH-invalidation hook, unlike
  `set_transform` which the Viewer pairs with `need_rebuild_`. Couple them in Wave 3/4.
- `ImplicitSDF`'s `hit_eps_` and its central-difference `eps = 1e-5` are absolute local lengths,
  so effective world tolerance scales with the object. No `ImplicitSDF` coverage in test_scale.cpp.
- `angles_from_direction({0,0,0})` returns NaN; uses `glm::normalize`, not `math::safe_normalize`.
- `auto_fit` does not validate `margin < 0` and produces inf/NaN on an inside-out AABB.
- `scripts/sweep.py:223` and `scripts/sweep_stl_rect_angles.py:1096` compute efficiency as
  `power / (DNI * aperture_area)`, valid only while `cos_ap == 1`. They become wrong by `1/cos`
  once the cosine is reachable from JSON.
- Reasoned risk, not demonstrated: the TriangleMesh epsilon loosening admits grazing hits that the
  fixed 1e-6 world ray offset may not clear, so watch for shadow acne on mesh scenes.

## Agent orchestration
For large parallelizable work, deploy a two-layer agent hierarchy:
- Fable commands. Opus agents lead major independent workstreams. Sonnet subagents do bounded
  implementation work with explicit file lists and acceptance criteria.
- **Waves are sequential; within a wave, file ownership is strictly disjoint.** Every affected file has
  exactly one owner. Cross-wave reuse is fine because waves never overlap in time.
- `CMakeLists.txt` and `vcpkg.json` are never owned by a workstream — leads report the lines they need and
  the wave integrator applies them.
- **Workers never self-certify.** Every completion claim must cite build output and named tests. "Done"
  without a green `ctest` line is rejected and re-issued.
- Important diffs get an independent Opus auditor that reads them cold, without the implementer's reasoning.
- The commander performs final integration and QA by re-deriving from the diff, build and test output —
  not by accepting agent reports.

## Platform
Windows 10/11, MSVC (VS 2025, v18) x64, CMake 4.x, Ninja generator, vcpkg at C:\dev\vcpkg in manifest mode. CMakePresets.json encodes MSVC/SDK paths (INCLUDE, LIB, PATH, compiler) so cmake --preset debug works from any shell without needing a Developer Command Prompt.

## Working style
- Plan Mode is ON. Propose plans before multi-file changes.
- Show diffs before writing large new files.
- After meaningful changes, run `cmake --build build/debug` and `ctest --test-dir build/debug --output-on-failure`. Report results.
- Commit in logical units with descriptive messages.
