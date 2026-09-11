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
- [x] Wave 2 — SceneDocument & JSON writer (the pivot everything downstream hangs off)
- [x] Wave 3 — SceneEditor & mutation ∥ Viewer decomposition + outliner
- [x] Wave 4 — ImGuizmo & transform UI ∥ model import & units
- [x] Wave 5 — Async trace, save UI, polish, packaging
- [ ] Wave 6 — AI helper

Out of scope on this track: thermal model, lat/lon geographic sun, TMY weather, day-integrated Wh.

## GUI redesign (current work)

Driven by the first real human testing of the app. Plan and an interactive layout mockup exist;
the mockup is the agreed target design. Phases 1 and 2 are done.

- [x] Phase 1 — Scale/rotation bug fixes, placements reach the document
- [x] Phase 2 — Polyscope panels hidden, ghost window killed, maximise on start, panels docked
      and re-flowing on resize (`include/scrt/viz/Layout.hpp`)
- [ ] Phase 3 — Dark/light themes + Settings panel (incl. per-panel toggles for Polyscope's own)
- [ ] Phase 4 — Stronger selection highlight; numeric entry for translate/rotate/scale
- [ ] Phase 5 — Left panel becomes three tabs; right column = flux / selection / contextual transform
- [ ] Phase 6 — AI helper + import/export overlays

Agreed design: dark default with a light option; a floating circular button opening a modal
overlay for the assistant; import/export share that styling but use the native Windows dialog;
Polyscope's own panels hidden with per-panel toggles in Settings.

### OPEN BUGS — reported by the user, NOT fixed

1. **The background/ground plane still changes while scaling.** `set_extents_frozen()` in
   `TransformPanel.cpp` freezes `automaticallyComputeSceneExtents` during a drag and calls
   `updateStructureExtents()` once on release - so the jump on release is expected, but the user
   reports movement during the drag too. Unverified whether the freeze engages on the first drag
   frame (it is called near the end of `draw_transform_panel`, possibly after the gizmo has
   already run that frame). **Recommended fix, not yet applied:** pin `groundPlaneHeightMode` to
   Manual at load so the floor stops following the scene bounding box at all. Do not re-diagnose
   this from the code alone - two previous diagnoses were confidently wrong.
2. **The flux map renders as RGB noise** - orange/blue/green confetti instead of a heat ramp.
   Never investigated. Note the stale `crash.log` at the repo root complains
   `unrecognized colormap name: plasma`, which is a likely lead.
3. **Edge-panning is not wired to the viewport rect.** `Layout.hpp` exposes `viewport_min/max`
   precisely so the trigger follows the viewport rather than the window, but nothing consumes it
   yet. The user explicitly wants edge-panning during a drag PRESERVED; now that panels occupy
   the screen edges it may have stopped working.

### How to run and test

`run.bat` (debug) and `run-release.bat` (optimised) at the repo root build and launch in one step.
Use the release one for anything above ~100k rays; debug tracing is roughly 10x slower.
`scripts/package.ps1 -Version X.Y.Z` produces the standalone zip.

**`--headless` does NOT exercise the GUI** - it returns before the viewer is constructed. A
startup crash once shipped with every headless check passing. After any viewer change, launch the
app and confirm it holds a window.

### Lessons that cost real time here

- **A harness that feeds synthetic input to the component under suspicion proves nothing.** An
  agent cleared ImGuizmo's LOCAL scale path by driving the panel's maths with matrices it made up,
  never with ImGuizmo's own output - which is exactly where the corruption was. The conclusion was
  reported upward and repeated to the user before anyone noticed what it had actually driven.
- **Prefer structural impossibility to a corrected calculation.** The scale/rotation bug was
  finally fixed by restricting each gizmo operation to its own components, not by getting the
  decomposition right.
- **Panels that are written but never called.** This happened three times - the import panel, the
  save panel, and `ctx.editor` never being assigned. When adding a panel, grep that something
  actually draws it and that its context fields are populated.
- Trust `git diff --stat` over any agent's completion report.

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
- **Dispatch agents FLAT in this harness - do not nest.** Both nesting modes fail: a sub-agent spawned
  with `run_in_background: true` (the default) leaves the parent with nothing to block on, so it reports
  "children running" and returns with unverified work; `run_in_background: false` makes the parent block
  silently and the stream watchdog kills it at 600s. There is no setting that lets a lead both wait for
  its children and stay alive. One agent, one bounded task, no sub-agents. This overrides the agent-army
  skill's rule that every Layer 1 agent must spawn 2+ sub-agents.
- The commander performs final integration and QA by re-deriving from the diff, build and test output —
  not by accepting agent reports.

## Platform
Windows 10/11, MSVC (VS 2025, v18) x64, CMake 4.x, Ninja generator, vcpkg at C:\dev\vcpkg in manifest mode. CMakePresets.json encodes MSVC/SDK paths (INCLUDE, LIB, PATH, compiler) so cmake --preset debug works from any shell without needing a Developer Command Prompt.

## Working style
- Plan Mode is ON. Propose plans before multi-file changes.
- Show diffs before writing large new files.
- After meaningful changes, run `cmake --build build/debug` and `ctest --test-dir build/debug --output-on-failure`. Report results.
- Commit in logical units with descriptive messages.
