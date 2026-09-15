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
- [x] Wave 6 — AI helper

Out of scope on this track: thermal model, lat/lon geographic sun, TMY weather, day-integrated Wh.

## v2 — optical simulator (current work, branch `feat/optical-simulator`)

Plan: `docs/plans/v2_optical_simulator.md`, with the Wave 2 design in
`docs/plans/v2_wave2_sources_design.md`, the Wave 3 library contents in
`docs/plans/v2_wave3_library_contents.md`, and Waves 4-8 in
`docs/plans/v2_waves_4_to_8_design.md`. Eight waves turning the solar
cooker tracer into a general optical simulator — bench optics, libraries of components and
materials, polarisation, interference, a diffraction engine, an optimiser, and an assistant with
tools. The sun becomes one source among several; there are no modes.

- [x] Wave 1 — Foundations: the scene is actually mutable
- [x] Wave 2 — General sources; the sun stops being special
- [ ] Wave 3 — Components and materials as libraries
- [ ] Wave 4 — Representative geometry, grid and snapping
- [ ] Wave 5 — Polarisation and interference
- [ ] Wave 6 — Diffraction, as a second engine
- [ ] Wave 7 — The optimiser
- [ ] Wave 8 — The assistant, with tools

### What Wave 1 fixed, and what it means for everything after

- **An element added at runtime is now drawn.** `ctx.add_element` used to reach the document and
  the BVH and nothing else, because `register_surfaces()` is called only from `register_scene()`,
  which runs only on load. Imported models were traced and saved while invisible. `Viewer::
  adopt_element` / `release_element` are the one path that makes the view match an element.
- **Element mutations are QUEUED and applied at the end of the frame.** `Scene::surfaces()` hands
  out a span over the vector these mutations resize, and panels iterate it while drawing — the
  outliner's Delete button sits inside such a loop. `add_element` therefore returns "accepted",
  not an id.
- **Material, sun and trace edits reach the document.** They used to write the live object only,
  so a slider move plus Save wrote the value the file was LOADED with. `SceneEditor` gained
  `commit_material_param`, `commit_sun` and `commit_trace_config` — cases 2, 3 and 4 of the
  header's rule 2. `commit_material_param` REFUSES a key the material's type does not have,
  because the document is re-read in strict mode and a stray key makes the file unopenable.
- **`io::build_surface` and `io::is_default_transform` are public**; SceneEditor's hand-maintained
  copies are gone. A new surface type is now added in one place, which Wave 3 needs six times.
- **A recorded ray path is a `RayPath` tree**, not a flat polyline, with per-edge power.

### What Wave 2 changed, and the numbers that guard it

Design: `docs/plans/v2_wave2_sources_design.md`, followed as written with the deviations listed
at its end. Shipped as v1.10.0.

- **A source states its own power in watts and originates its own rays.** `sources::LightSource`
  (`total_power_w`, `sample_ray(Rng&)`, `type_name`, `as_sun`). The sun OWNS its aperture;
  `Scene` has no aperture, only a presentation-only `display_aperture()`. `Scene::sun()` is gone;
  `primary_sun()` returns the first solar source and every one of its callers is a place still
  coupled to the sun. `tracer::EmissionPlan` divides the global ray index space among sources
  by power, before the parallel region, so slot seeding is untouched.
- **A laser exists** (`sources::Laser`: watts, wavelength, beam diameter, FULL-angle divergence
  in mrad). `examples/laser_bench.json` is the only shipped file spelt with `"sources"`.
- **The document is a list**: `SceneDocument::sources`, `variant<SunSourceDoc, LaserSourceDoc>`,
  visited with `io::overloaded` and no catch-all in both the writer and the loader. Legacy
  `"sun"` + `"aperture"` still parses; both spellings at once is a hard error in strict AND lax
  mode. **The writer is legacy-preferring**: one sun and nothing else is written the old way, so
  all 94 shipped scenes re-save identically and the assistant prompt did not change. That was
  MEASURED: write_document() output for all 94 was dumped before and after Stage 4 and diffed.
- **Reporting without a sun**: `concentration_ratio` is omitted (summary JSON, headless stdout,
  the compare CSV cell is empty, the flux window says "not defined") rather than computed
  against a fictitious 1000 W/m2. The exporter takes `std::optional<double>` DNI.
- **A fifth golden**: `examples/dispersive_lens.json` (the Fresnel cooker with
  `"sellmeier": "bk7"`) at 38.763765222589399 W. None of the four originals reads the wavelength
  at all, so they would sit still while a source quietly changed colour. Wavelength is also
  checked end to end in `tests/test_laser.cpp`: a 45-degree pencil beam into BK7 lands where
  Snell says at 400 nm and at 1064 nm, read from the recorded path.

Instruments used for the gates, worth keeping: `scrt_compare` now writes 17 significant digits
(it wrote 6, a thousand times coarser than the 1e-9 golden gate). A 94-scene release CSV was
captured twice at Stage 0 (identical) and diffed at every stage; every scene pins a non-zero
seed, none pins a thread count, so the baseline is reproducible here (20 slots) but not on a
different core count.

**Harness gap found and NOT fixed:** `scripts/screenshot_app.ps1` photographs the desktop
wallpaper for `examples/laser_bench.json` while the app is alive, `Responding`, and a direct
`CopyFromScreen` of the same window captures it correctly (`build/s5_laser_scene.png`). It
works for every solar scene. Suspect the capture path, not the app; the sun panel's no-sun
branch was therefore checked by build and by reading, not by eye.

An independent cold audit (Opus, read-only) of the Stage 1-4 diffs found no bit-identity,
allocation, thread-safety or schema defect, and three lesser ones that were fixed: the laser
cone was a small-angle sampler that accepted any angle (now exact over the cap and bounded at
180 degrees), an all-dead-source scene returned without finalizing the receiver, and a null
source reached an unchecked dereference. Details at the end of the design document.

### Wave 3 progress (in flight)

Decisions taken with the user before code: new surface types rather than a clip on `plane`
(1-B); realistic absorbers are low-albedo diffusers rather than a parameter on `absorber` (2-C);
the thick lens is one closed solid (3-B). Every stage gates on the five goldens (`==`) and the
corpus diff; the tracer, `plane` and every existing material are untouched in this wave.

- [x] Stage 0 - baseline: 96-scene corpus captured twice at HEAD, identical; suite green
- [x] Stage 1 - `SceneEditor::add_material/remove_material/add_source/remove_source`; `io::build_material` and `io::build_source` factored out of `build_scene` so the editor and a file load derive through one builder
- [ ] Stage 2 - gap G3: `disk` (optional hole = iris) and `slit_plate` surfaces, new types so `plane` is untouched; parser, writer, loader, prompt and schema test
**Engine bug found by Stage 2's first real scene, fixed in its own commit:** the accumulator
overload of `Tracer::trace_one` (headless, `scrt_compare`, the GUI preview) deposited on ANY
absorbed hit, binned by that surface's own local coordinates, so an absorbing iris in front of the
screen was booked as light ON the screen and a headless trace read the full beam power. The
receiver overload (the goldens) deposited only on receiver faces. Now both do. No shipped scene
binds an absorber to an element, which is why the corpus never showed it and why the fix moves
no corpus result; `tests/test_apertures.cpp` pins the two overloads equal with `==`. Found by
measuring, not by reading: the unit tests built the scene by hand through the receiver overload
and passed.

- [ ] Stage 3 - `beam_splitter` material: designed R, absorptance, `Split` at every angle
- [ ] Stage 4 - `thick_lens` closed solid (two caps + rim); Sellmeier presets for N-SF11, PMMA, polycarbonate, soda-lime, low-iron, water (G2)
- [ ] Stage 5 - `diffuser` material (Lambertian, albedo); realistic blacks are low-albedo diffusers (G1)
- [ ] Stage 6 - material library (presets + user entries beside the AI config), component catalogue, `examples/optical_bench.json`

### The measurement Wave 1 settled, worth not repeating

Only `fresnel_lens_cooker.json` splits among the golden scenes — 19617 splits per 20k-ray trace —
and its deepest TOTAL path is **2**, against `max_bounces: 8`. So the old fresh-bounce-budget bug
was unreachable in every golden scene, and fixing it moved nothing. If a future change needs to
know whether paths are hitting the bounce limit, that is the number, and instrumenting
`trace_one` with a thread-local depth counter is a ten-minute job.

## GUI redesign (v1, complete)

Driven by the first real human testing of the app. Plan and an interactive layout mockup exist;
the mockup is the agreed target design. Phases 1 and 2 are done.

- [x] Phase 1 — Scale/rotation bug fixes, placements reach the document
- [x] Phase 2 — Polyscope panels hidden, ghost window killed, maximise on start, panels docked
      and re-flowing on resize (`include/scrt/viz/Layout.hpp`)
- [x] Phase 3 — Dark/light themes + Settings panel (incl. toggle for Polyscope's own panels)
- [x] Phase 4 — Stronger selection highlight; numeric entry for translate/rotate/scale
- [x] Phase 5 — Left panel becomes three tabs; right column = flux / selection / contextual transform
- [x] Phase 6 — AI helper + import/export overlays

Also done outside the phase list: an embedded Font Awesome subset (`scripts/gen_icon_font.py`
regenerates `include/scrt/viz/Icons.hpp` and `src/viz/IconFontData.cpp`), and DPI scaling, which
matters more than it sounds — at 250% the interface was unusable and nobody had seen it, because
nobody had ever looked at the app on a high-DPI screen.

Agreed design: dark default with a light option; a floating circular button opening a modal
overlay for the assistant; import/export share that styling but use the native Windows dialog;
Polyscope's own panels hidden with per-panel toggles in Settings.

### The scene assistant

`src/viz/panels/AiOverlay.cpp` + `src/viz/ClaudeClient.cpp` (WinHTTP) + `src/viz/AppConfig.cpp`.
Runs on the user's own Anthropic API key, kept in `%APPDATA%\solar-cooker-rt\config.json` as plain
text; generated scenes land in `generated/` beside it and are loaded by path, through the same
deferred loader everything else uses.

**The system prompt is a contract with `io::parse_document`, and `tests/test_assistant_schema.cpp`
is the only thing holding the two together.** Replies are parsed with `strict=true`, so a key the
prompt invents is a hard failure — which is the point, but it also means a stale prompt breaks
*every* generation, and only for a user who has a key. Whenever a surface type, material type or
key changes in `SceneDocument.cpp`, update the prompt and that test. The test earned its place on
its first run: the prompt said `{"type": "absorber", "reflectance": 0.05}` and an absorber takes no
parameters at all.

The wire call has now been exercised by the user - `%APPDATA%\solar-cooker-rt\generated\` holds a
real generated scene, and it loaded. Note that the model used `aperture.mode` and
`aperture.margin`, which the strict parser accepts but the prompt does not mention; the prompt is
a floor on what it will use, not a ceiling.

### OPEN BUGS — reported by the user, NOT fixed

1. ~~Background changes while scaling~~ - FIXED. `groundPlaneHeightMode` is pinned to Manual at
   registration with a height taken from the scene's own bounds, so the floor no longer tracks the
   bounding box. Polyscope was also left at its default Y-up while this project is Z-up throughout,
   which drew the ground as a tilted wall through the cooker; `UpDir::ZUp` is now set.
   **Not yet confirmed by a human at a window.**
2. ~~Flux map renders as RGB noise~~ - FIXED, and the earlier diagnosis here was wrong.
   It was called "not a bug, just Poisson noise". The noise is real - 10k rays over a 64x64
   receiver is ~2 rays per bin, and note the trade-off runs opposite to intuition, since a FINER
   grid makes it worse - but that was not why it looked like confetti. **`FluxPlotter` never
   pushed a colormap, so `PlotHeatmap` used ImPlot's default `ImPlotColormap_Deep`: a TEN-COLOUR
   CATEGORICAL palette.** Smooth variation was being rendered as random colour jumps, in a
   different colour scheme from the 3D receiver beside it, which used viridis. Both views now
   sample one ramp (`ViewSettings.cpp`), and there is a smoothing slider. The preview is also
   150k rays (`kPreviewRays` in `Viewer.cpp`).

   The lesson: "that is just noise" was a plausible explanation that fitted the symptom and
   stopped the investigation. Viridis contains no pink and no red, and the screenshot was full
   of both - which was visible in the very image used to dismiss it.
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
- **Measure before concluding, even when the explanation fits.** "The flux map is just Poisson
  noise" fitted the symptom, was written into this file as settled, and was wrong about the part
  that actually mattered. Two minutes reading which colormap `PlotHeatmap` defaults to would have
  found it. The same turn also burned twenty minutes guessing at a vector-subscript assert that
  four `fprintf` calls located immediately.
- **Two agent-tooling facts that cost real time in Wave 2.** A shell command over roughly
  160 lines is truncated by the harness and fails with "unexpected EOF" without running at all;
  split edits into short scripts or write files with the file tool. And a Python script fed on
  stdin is decoded in the Windows code page, so a pattern containing a non-ASCII character
  (the superscript in "W/m2", an em dash) silently matches nothing; run such scripts from a
  file, where the source is read as UTF-8. Backslashes suffer the same way: a doubled
  backslash in a heredoc reaches Python single, so a pattern for a C string with "\\n" never
  matches and a replacement containing one writes a real newline into the source.
- **Polyscope's `ValueColorMap::getValue(1.0)` reads one past the end of its own table.** It
  blends `values[lowerInd]` with `values[lowerInd + 1]` with no upper guard. Never sample a
  Polyscope ramp at exactly 1.0; `ViewSettings.cpp` stops a hair short.
- **ImPlot's `LerpTable` does not clamp for a continuous colormap.** `scale_max` passed to
  `PlotHeatmap` must be >= every value in the array *as float*, or the colour lookup runs off the
  end of the table. Take the max over the converted floats, not over the doubles they came from.

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
