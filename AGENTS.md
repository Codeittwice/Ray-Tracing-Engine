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
- [x] Phase 3 — Dark/light themes + Settings panel (incl. toggle for Polyscope's own panels)
- [x] Phase 4 — Stronger selection highlight; numeric entry for translate/rotate/scale
- [x] Phase 5 — Left panel becomes three tabs; right column = flux / selection / contextual transform
- [ ] Phase 6 — AI helper + import/export overlays

Also done outside the phase list: an embedded Font Awesome subset (`scripts/gen_icon_font.py`
regenerates `include/scrt/viz/Icons.hpp` and `src/viz/IconFontData.cpp`), and DPI scaling, which
matters more than it sounds — at 250% the interface was unusable and nobody had seen it, because
nobody had ever looked at the app on a high-DPI screen.

Agreed design: dark default with a light option; a floating circular button opening a modal
overlay for the assistant; import/export share that styling but use the native Windows dialog;
Polyscope's own panels hidden with per-panel toggles in Settings.

### OPEN BUGS — reported by the user, NOT fixed

1. ~~Background changes while scaling~~ - FIXED. `groundPlaneHeightMode` is pinned to Manual at
   registration with a height taken from the scene's own bounds, so the floor no longer tracks the
   bounding box. Polyscope was also left at its default Y-up while this project is Z-up throughout,
   which drew the ground as a tilted wall through the cooker; `UpDir::ZUp` is now set.
   **Not yet confirmed by a human at a window.**
2. ~~Flux map renders as RGB noise~~ - NOT A BUG. It is Poisson noise from the automatic
   preview: 10k rays over a 64x64 receiver is ~2 rays per bin. Note the trade-off runs opposite
   to intuition - a FINER receiver grid makes it worse, since each bin catches fewer rays. The
   preview is now 150k rays (`kPreviewRays` in `Viewer.cpp`); a full trace smooths it further.
3. **Edge-panning is not wired to the viewport rect.** `Layout.hpp` exposes `viewport_min/max`
   precisely so the trigger follows the viewport rather than the window, but nothing consumes it
   yet. The user explicitly wants edge-panning during a drag PRESERVED; now that panels occupy
   the screen edges it may have stopped working. Note that no code in this repo implements
   edge-panning, so whatever the user is seeing comes from Polyscope's own camera handling -
   do not "restore" a feature by writing a new one without first confirming what it actually is.
4. **The interface does not re-scale when the window is dragged to a differently-scaled
   monitor.** `ui_scale()` is fixed at startup because the font atlas is rasterised once and
   Polyscope shares it across every ImGui context it creates. Re-scaling means rebuilding the
   atlas, which Polyscope owns.

### How to run and test

`run.bat` (debug) and `run-release.bat` (optimised) at the repo root build and launch in one step.
Use the release one for anything above ~100k rays; debug tracing is roughly 10x slower.
`scripts/package.ps1 -Version X.Y.Z` produces the standalone zip.

**`--headless` does NOT exercise the GUI** - it returns before the viewer is constructed. A
startup crash once shipped with every headless check passing. After any viewer change, launch the
app and confirm it holds a window.

`scripts/screenshot_app.ps1` launches the app, photographs its own window and closes it:

```
powershell -ExecutionPolicy Bypass -File scripts/screenshot_app.ps1 `
    -Exe build/debug/scrt_app.exe -Out build/shot.png -WaitSeconds 30 -Clicks "0.07,0.20"
```

`-Clicks` is a `;`-separated list of `x,y` fractions of the window rect, clicked in order before
the shot, so a panel that only appears with something selected can actually be photographed. It
raises the window topmost first, because Windows refuses `SetForegroundWindow` to a background
process and a terminal left sitting over the app otherwise ends up in the picture. It must NOT
use `SW_RESTORE` - that un-maximises the window it is about to photograph, which cost an hour
of chasing a maximise bug that did not exist.

Every layout problem fixed in the icon-font pass was found this way and none of them were
visible in a normal-DPI window.

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
- **A diagnostic harness can be the thing that is broken.** The screenshot script reported the
  window as 1302x776 and not maximised; the app was maximised at 3862x2110 and the script's own
  `SW_RESTORE` was un-maximising it. Before concluding the program is wrong, check that the
  instrument is not.

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
