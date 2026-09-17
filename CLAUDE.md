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
- [x] Wave 3 — Components and materials as libraries
- [x] Wave 4 — Representative geometry, grid and snapping
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

### What Wave 3 added (complete, shipped as v1.11.0)

Decisions taken with the user before code: new surface types rather than a clip on `plane`
(1-B); realistic absorbers are low-albedo diffusers rather than a parameter on `absorber` (2-C);
the thick lens is one closed solid (3-B). Every stage gates on the five goldens (`==`) and the
corpus diff; the tracer, `plane` and every existing material are untouched in this wave.

- [x] Stage 0 - baseline: 96-scene corpus captured twice at HEAD, identical; suite green
- [x] Stage 1 - `SceneEditor::add_material/remove_material/add_source/remove_source`; `io::build_material` and `io::build_source` factored out of `build_scene` so the editor and a file load derive through one builder
- [x] Stage 2 - gap G3: `disk` (optional hole = iris) and `slit_plate` surfaces, new types so `plane` is untouched; parser, writer, loader, prompt and schema test
**Engine bug found by Stage 2's first real scene, fixed in its own commit:** the accumulator
overload of `Tracer::trace_one` (headless, `scrt_compare`, the GUI preview) deposited on ANY
absorbed hit, binned by that surface's own local coordinates, so an absorbing iris in front of the
screen was booked as light ON the screen and a headless trace read the full beam power. The
receiver overload (the goldens) deposited only on receiver faces. Now both do. No shipped scene
binds an absorber to an element, which is why the corpus never showed it and why the fix moves
no corpus result; `tests/test_apertures.cpp` pins the two overloads equal with `==`. Found by
measuring, not by reading: the unit tests built the scene by hand through the receiver overload
and passed.

- [x] Stage 3 - `beam_splitter` material: designed R, absorptance, `Split` at every angle
- [x] Stage 4 - `thick_lens` closed solid (two caps + rim); Sellmeier presets for N-SF11, PMMA, polycarbonate, soda-lime, low-iron, water (G2)
- [x] Stage 5 - `diffuser` material (Lambertian, albedo); realistic blacks are low-albedo diffusers (G1)
- [x] Stage 6 - material library (presets + user entries beside the AI config), component catalogue, `examples/optical_bench.json`

### Wave 4 (complete, shipped as v1.13.0; corpus 98/98 identical)

Design: `docs/plans/v2_waves_4_to_8_design.md` sections 4.1-4.8. Gating follows the reduced
policy above: goldens and full ctest every stage, corpus once at the end (nothing in this wave
touches the tracer, an intersect or an interact).

- [x] Stage 1 - appearance dispatched on MATERIAL TYPE, not on a name match
- [x] Stage 2 - library previews: component thumbnails and material diagrams
- [x] Stage 3 - drag a component from the library into the 3D view
- [x] Stage 4 - snapping (ImGuizmo's `snap` argument, plus the numeric fields and a library drop)
- [x] Stage 5 - procedural geometry (mirror substrates, splitter cubes, posts)
- [x] Stage 6 - the grid, and the Polyscope extents trap it triggers

**Two things measured rather than assumed, both of which would have shipped wrong:**

1. The default scene's cooker went gold -> grey and a band in it magenta -> white after Stage 1.
   I suspected the receiver's flux mesh had been restyled. One `fprintf` in the styling path
   showed it touches exactly three surfaces, all reflectors - no receiver involved. The old
   colours were Polyscope's per-structure palette, which is the point of the change: three
   mirrors were three arbitrary colours and are now one mirror colour. The white is candy's
   specular highlight. Nothing was broken.
2. **`view::screenCoordsToWorldPosition` returns infinity when called from inside the ImGui user
   callback**, even with the cursor squarely on an object - it reads the depth buffer, and from
   there the read comes back empty. A component dropped on the cooker landed 1.8 m away on the
   ground-plane fallback, at a position that looks perfectly plausible in isolation. The drop now
   casts the camera ray against `Scene::intersect` instead: exact, no buffer read, and it is the
   geometry the tracer actually traces. VERIFIED by dropping on the box cooker and checking the
   read-out against the scene file: (0.375, 0.000, 0.310) lies on the east reflector's plane to
   within 0.1 mm and well inside its extent. Do not "simplify" this back to the depth query.

The selection read-out now reports the object's CENTRE as well as its extent. Its own empty-state
hint promised "what it is and where it sits" and the position was the half that was missing; it
is also the only way to read back where a dragged component landed.

**Snapping (Stage 4).** One `SnapSettings` (`include/scrt/viz/Snap.hpp`) read by the gizmo, the
Move/Turn/Size fields and a library drop, so the three placement paths cannot round differently.
Two things that are not obvious from the design note:
- Rounding the value a `DragFloat` edits in place makes the field STICK: ImGui moves it by a
  sub-step delta and the rounding takes it straight back. `snapped_drag` lets the drag edit an
  unrounded shadow while the field is active and writes out only the snapped value.
- ImGuizmo snaps the delta FROM DRAG START, so an object that started off-grid would land on
  start + k*step. The triple is rounded again after write-back so the gizmo and the fields agree.
  Locked axes are skipped; uniform scale rounds the mean and rescales, like the Scale field.
Fields snap the OFFSET they display (from the file's position); a drop snaps its world position,
which can leave the origin up to half a step off the surface it was dropped on (the status line
says "snapped"). VERIFIED by eye: back_reflector, snap on at 0.01 m, an arbitrary 61 px drag on the
Y handle moved the centre 0.350 -> 0.450 and the field reads 0.1000. The snap-OFF control run was
not captured (the harness photographed another window twice), so "off is continuous" rests on the
code path being the pre-Stage-4 one: step 0 calls plain `DragFloat` and passes ImGuizmo `nullptr`.

**Bodies (Stage 5) are STORED in the scene, decided with the user.** The plan did not say what
makes an element get a substrate, cube or post, and inferring it is not honest: a 50:50 cube and a
50:50 plate are the same flat `beam_splitter` surface to the engine. So an element carries an
optional `"body": {"substrate_m", "cube", "post"}` (`io::BodyDoc`), parsed in both modes (a
negative slab or cube+substrate is refused), written only as set, in the assistant prompt and its
schema test. **Nothing that traces reads it.** `examples/optical_bench.json` gained posts, so the
end-of-wave corpus diff checks that claim on a real scene.
- Geometry is `src/viz/Body.cpp` (pure maths, unit-tested, also compiled into `scrt_tests`).
  Substrate: behind the surface on local -Z, matching its outline (round for a `disk`). Cube: the
  flat surface is the diagonal of the square cross-section in local x-z. Post: 12.7 mm, vertical,
  under the part's centre, omitted when the part is at the floor.
- Bodies are NOT baked-and-`setTransform`ed like surfaces: they are re-tessellated in world space
  on every placement edit (`RayRenderer::sync_body`), because a post must stay upright and reach
  z = 0 however the part is turned, which no rigid transform of the uploaded mesh can do. A body
  is a few dozen vertices; same-count updates use `updateVertexPositions`.
- `StructureRegistry` keeps bodies by id, and `erase()` deliberately does NOT drop them:
  `intern()` calls `erase()` on every re-registration, and the transform panel's renderer has no
  document to put them back from.
- Library: bench parts (mirrors to 2 inch, lenses, splitters, apertures, detectors) get a post,
  flat mirrors a 6 mm substrate, and a new "Beamsplitter cube, 1 inch" a cube. Cooker-scale parts
  get nothing. The honesty note ("drawn only; a cube is traced as its diagonal coating") is a help
  line at the top of the Components list and in the cube entry's own note.
- VERIFIED by eye on a scratch scene (square mirror + substrate, cube, round mirror + substrate,
  all on posts): slab dark and behind, cube translucent with the post visible through it and the
  1.41:1 silhouette of a cube seen corner-on, round substrate visible as rim thickness.
  NOT looked at: `optical_bench.json` itself (a laser scene, which the screenshot harness cannot
  photograph) and a drag of a part with a post (the re-tessellation path is exercised only by
  reasoning and by the unit tests of the geometry).

**The grid (Stage 6)** is a `CurveNetwork` at the move-snap step (`sync_grid`, called every frame,
works only when the setting or step changed or a load removed it), toggled in Settings. It is
sized from **Polyscope's own `state::boundingBox`**, recomputed with the old grid removed, and
lies inside it, so it adds nothing to the extents. The first version used the OPTICAL scene's
bounds: the user's screenshot of a bench showed the grid floating 7.5 cm up at the lowest optic
while every post ran through it to the real floor (bodies are not in the optical bounds).
Measured on that first version: `lengthScale` 1.30000007 -> 1.30000007 on the default scene; the
current version keeps the property by construction (a structure inside the box cannot grow it).
Grid line radius is absolute: `max(0.02 x spacing, 0.0005 x diagonal)` - 0.2 mm lines drew as
speckle on a 1.3 m cooker. A click on a post or substrate selects the part it belongs to.

**Asked for by the user after Wave 4 shipped (committed during Wave 5):**
- Posts are 6 mm (`kPostRadius` 0.003), black, and hidable (Settings > Show posts). Substrates and
  cubes cannot be hidden: they are what the part IS, not what it stands on.
- **The design note's "a CurveNetwork cannot opt out of the scene extents" is wrong.**
  `Structure::hasExtents()` is virtual and CurveNetwork does not override it, so
  `ExtentlessCurveNetwork` (RayRenderer.cpp) returns false and is registered with
  `polyscope::registerStructure`. The grid now reaches 30% beyond the scene on every side, and the
  alignment axis is drawn the same way; neither can move the ground plane or a relative length.
- **Alignment axis** (`include/scrt/viz/Align.hpp`, tool state, not saved): starts on the first
  laser's beam, re-settable from any part ("Set axis from this part": through its centre along its
  local +Z). "Centre on axis" moves the part's centre onto the line (locked axes skipped); "Face
  along axis" turns it about its own centre so local +Z lies on the line, never flipping it over
  (Rodrigues onto the nearer end). VERIFIED by readout on QA 03: the white card at (0.100, 0.150,
  0.100) went to (0.100, -0.000, 0.100) with Centre, and from extent 0.050 x 0.000 x 0.050 to
  0.000 x 0.050 x 0.050 with Face, centre unchanged.

**User-reported, fixed alongside Stage 6:**
- Ray defaults are now 0.3 mm and opacity 0.20 (the user's chosen values).
- The Settings window opened wide and shrank into place: `AlwaysAutoResize` fits the content
  while its width -1 widgets fit the window, and they chased each other down. Width is now a
  size CONSTRAINT. Measured by frame burst: 697 px in every frame from 335 ms after the click
  (the first 335 ms were not captured).
- **Scenes used to test a feature live in `examples/feature_checks/`** so the user can open them.
  That folder is NOT in the corpus's four directories. `w4_bodies_check.json` is a LASER bench:
  an earlier version swapped the laser for an overhead sun to dodge the screenshot harness, and
  the user rightly called the resulting picture wrong (sun rays grazing vertical mirrors from
  above). Don't change a test scene's physics to suit the instrument. The harness photographed
  that laser scene correctly this time, so the earlier "laser scenes capture the wallpaper" gap
  is intermittent, not systematic.
- **A running `scrt_app.exe` locks the exe and every rebuild fails at link (LNK1104)**, which
  `run.bat` shows as a red FAILED - and the user then runs the STALE build and reports that the
  fix did not work. Check `Get-Process scrt_app` before concluding a fix failed.

Tooling: from the PowerShell tool, `cmake --build build/debug` fails with "cannot open float.h"
(no MSVC environment); `cmake --build --preset debug` works.

Not verified: drag and drop at a second display scale. Polyscope passes `ImGui::GetMousePos()`
straight to its own picking, so the window-to-buffer scaling is inside Polyscope rather than in
this code, but that reasoning has not been checked against a scaled monitor.

### Wave 5 progress (in flight) - polarisation and interference

Design: `docs/plans/v2_waves_4_to_8_design.md` section 5. Touches the tracer, Fresnel and the
schema, so the corpus runs after every such stage and a cold audit closes the wave.

Decided with the user before code:
- **Polarisation is OPT-IN on a laser** (`"polarisation"`, default unpolarised), so every corpus
  scene stays identical; library lasers set it realistically. Unpolarised rays run the old code.
- **A coherent receiver REFUSES a randomly sampled source** (hard error saying why): random rays
  summed with phase are speckle that looks like fringes.
- **Ray size:** measure the corpus before and after; if it is more than about 10% slower, stop and
  show the user before splitting the ray type.

- [x] Stage 0 - baseline corpus captured twice for timing spread
- [x] Stage 1 - Fresnel returns rs/rp; the unpolarised average bit-identical (`==` sweep)
- [x] Stage 2 - Ray payload: polarised flag, Jones vector + s-axis, optical path; nothing reads it

**Stage 2 measured the ray-size cost, and there is none.** `sizeof(core::Ray)` went 72 -> 152
bytes (the design note's "48 bytes, roughly doubles" was wrong on the starting size; the test in
`tests/test_vec.cpp` records 152). Release corpus, 99 scenes, summed `wall_time_s`: before, four
captures of 20.59 / 21.10 / 20.86 / 21.52 s; after, two of 19.96 / 20.18 s. No slowdown within a
+-2% noise band - the tracer holds rays as stack locals one bounce at a time, never in bulk, so
the "memory-bound" worry did not apply. No split ray type is needed. Corpus: 0 differences.
- [x] Stage 3 - polarised rays through existing mirrors and dielectrics; source polarisation key

**Stage 3.** `optics/Polarisation.hpp`: a polarised ray's Jones vector is re-expressed in each
interface's s/p frame (`align_to_interface`, s = direction x normal), multiplied by amplitude
coefficients (`apply_jones`, which returns the branch's power fraction and renormalises - power
stays in `Ray::power`), and read back as a world field (`world_field`) by the tests, so a basis or
sign slip shows as the wrong PHYSICAL field. Every material branches on `r.polarised` and the
unpolarised code is untouched (Dielectric's polarised path is a separate function); corpus after
Stage 3: 100/100 identical. Per material: dielectric = exact s/p Fresnel with complex TIR phases;
perfect and real mirrors = conductor phases (rs -1, rp +1) about the (perturbed) normal; beam
splitter = designed power ratio for both, conductor phases (a coating's real phases are not
described by the material); thin pane = the incoherent slab done per polarisation, internal phases
not tracked; diffuser depolarises. A laser takes `"polarisation"`: `"unpolarised"` (default, omitted
on write), `{"linear_deg": a}` (0 = vertical on a horizontal beam), `"circular_left"`/`"circular_right"`
(left = counter-clockwise looking into the beam, (1, +i)/sqrt 2). The assistant prompt has no laser
section at all, so nothing there changed.

VERIFIED end to end on two QA scenes (a glass window at Brewster's angle, screen on the reflected
beam): p-polarised reads exactly 0 W; s-polarised reads 25.53% of the beam against 25.55% predicted
(front face cos^2(2 theta_B) = 14.8%, back face 10.7%). The s scene proves the screen is placed right,
which is what makes the p scene's zero mean something.
- [x] Stage 4 - polariser, waveplate, polarising beam splitter (Malus, quarter-wave checks)

**Stage 4.** `materials/PolarisingOptics.{hpp,cpp}`: `polariser` (transmission_axis_deg,
extinction_ratio, transmission = principal k1), `waveplate` (retardance_waves, fast_axis_deg,
transmission; same retardance at every wavelength - a zero-order plate at its design wavelength),
`polarising_beam_splitter` (extinction_ratio; p through, s reflected, 1/ER leak each way). Axes are
in the PART's own plane from its local +X (`h.surface->transform()`), so turning the part turns the
axis; a disk faced along a +X beam with `[0, 90, 0]` has 0 degrees vertical, the same as a laser's
`linear_deg`. Unpolarised light: a polariser passes k1(1+1/ER)/2 and leaves fully polarised on its
axis (partial polarisation cannot be one Jones vector; the folded leak is below 1e-3 at any real
ER); a waveplate passes it unchanged; a PBS splits exactly 50:50 into pure s and pure p. Registered
in the strict schema, loader, editor (`commit_material_param`), Design-tab sliders, appearance,
assistant prompt + schema test, and the library (six materials, four 1-inch components incl. a PBS
cube). **The library note below that a polarising beam splitter is "absent by design" is now out of
date: it ships.**

VERIFIED through the full engine (release `scrt_compare`, not just unit tests): a vertical laser
through an ideal polariser at 60 deg reads 1.250000 mW of 5 (cos^2 60 = 25%), crossed at 90 reads 0;
vertical -> quarter-wave at 45 -> analyser reads 2.500000 mW at 30 deg AND at 120 deg (circular),
and 0 with the plate's fast axis at 0 and the analyser crossed (plate does nothing). QA 08 and 09.
Unit tests (`tests/test_polarising_optics.cpp`): Malus over 0-90, 1/ER when crossed, unpolarised
through a sheet polariser, QWP linear -> circular -> crossed, HWP rotates by twice its axis, PBS arms
and leak. NOT fixed, noticed on the QA 09 screenshot: the outliner files every element under
"Reflectors" (a waveplate, a polariser, and - as before - every lens). A naming bug in the tree.
- [x] Quick features from user testing (before Stage 5; none depends on the physics still to come)
  - [x] Q1 - double-click a scene in the Scene Browser to load it
  - [x] Q2 - a toolbar over the 3D view with Preview and Full Trace, so a design check needs no tab switch (the Simulate tab stays)
  - [x] Q3 - a drawn laser body: a housing ending at the source's origin, beam leaving its front
  - [x] Q4 - finer receiver grids on the laser QA scenes (about 0.1 mm bins; a 2 mm beam covered 5 bins at 48x48)

**Quick features, as built.** Q1: the browser is `Selectable` rows (a `ListBox` cannot report a
double-click), 14 rows tall instead of 6, with a hint line; VERIFIED by a real double-click on
`parabolic_dish.json` that replaced the QA 03 tree with `primary_dish` and a Sun. Q2:
`draw_trace_toolbar` (TracePanel.cpp) floats at the top of the viewport rect and calls the SAME
`ctx.run_trace` / `ctx.cancel_trace` as the Simulate tab; shows progress + Cancel while running and
"on target: X mW" after. Q3: `RayRenderer::register_sources` draws each laser as a 25 x 120 mm
housing ending at the origin, a wavelength-coloured exit window the beam's size, and a black post;
re-run on Show posts. Presentation only. Q4: QA receivers regridded to 0.1 mm bins (QA 04 kept at
0.05 mm - its slits are 0.1 mm); total power unchanged on every scene, peaks now resolved (QA 01
focus 11.8 -> 115 kW/m2).

**Q4 found a real crash, fixed:** the flux heatmap drew one quad per receiver bin into an ImGui draw
list with 16-bit indices, so a 200x200 receiver (160k vertices) aborted the debug build ("Too many
vertices in ImDrawList") and would have drawn garbage in release. Any grid above about 128x128 would
have done it; no scene had one. `FluxPlotter` now block-averages to at most 120 cells a side for
DRAWING only - the stated power and hottest spot still come from the raw bins, and the 3D receiver
(Polyscope's own GL) is unaffected.
- [x] Stage 5 - optical path length with the index inside a dielectric

**Stage 5.** Both `trace_one` overloads add `medium_n * h.t` to `Ray::opl_m` on every hit, before the
receiver or the material sees the ray. `Dielectric` (both paths) sets the transmitted ray's
`medium_n` to the index it enters (glass, or 1 on the way out); `ThinDielectricPane`, which has no
geometric thickness, adds n t / cos(theta_t) to the transmitted ray. Two nudges matter at optical
scale: a pass-through receiver face moves the ray on by `EPSILON_T` = **1 um, more than a
wavelength**, so that step is added to the path too; the pane nudges its transmitted origin by 1e-7 m
and adds it back. Pure bookkeeping - no power, direction or draw changes: corpus 100/100 identical.
VERIFIED through the real `Tracer` with a recording probe material (`tests/test_optical_path.cpp`):
air 0.800 m; a 10 mm n = 1.5 window 0.805 m direct and 0.835 m for the first internal ghost (two
extra passes of n t); a 4 mm pane 0.806 m.
- [x] Stage 6 - coherent receiver, coherence length, grid sampling, Michelson example

**Stage 6.** A receiver with `"coherent": true` sums ray FIELDS. Pieces, each gated:
- **Sources:** `LightSource::sample_ray_indexed(rng, k, n)` (default = `sample_ray`, same draws) and
  `deterministic_sampling()`. A laser takes `"sampling": "grid"` - a Vogel sunflower spiral over the
  beam disk, radical-inverse over the cone, no Rng draw - and `"coherence_length_m"` (0 = fully
  coherent). The random path draws in the same order as before: corpus 100/100 identical.
- **Ray tags** (`Ray` 152 -> 160 bytes): `source` (index in `Scene::sources()`) and `branch`, a hash
  of (surface id, reflected/transmitted) folded in at every interaction by the tracer.
- **`FluxAccumulator` coherent mode.** THE DESIGN NOTE'S "sum a exp(i phase), take |sum|^2" IS WRONG:
  N in-phase rays of one beam give N^2 p, and normalising by primary rays fails the moment a mirror
  tilt sends a primary's two arms into different bins. What is implemented: each ray adds
  sqrt(P) exp(i 2 pi opl / lambda) - along its Jones vector, or a scalar channel if unpolarised - to
  an ARM of its bin keyed (source, branch); an arm is normalised by its own count of distinct primary
  rays (a change of `Ray::id` within a slot, since a primary's tree is traced before the next); arms
  of one source add as fields with the cross term weighted by exp(-|mean path difference| / Lc);
  different sources never interfere. The phase is carried to the bin CENTRE along the ray (local
  plane wave). `incoherent_power_w()` keeps the plain sum alongside.
- **Refusal (decided with the user):** `build_scene` refuses a coherent receiver with any source that
  is not grid-sampled, with the reason; the tracer refuses the same (a laser added from the library
  after load), and the GUI worker CATCHES it and the toolbar shows the message in red - a throw out
  of a `jthread` would have been `std::terminate`.
- The accumulator-overload `run` (GUI preview, `scrt_compare`) makes the caller's accumulator
  coherent exactly when the scene's receiver is.

VERIFIED (`tests/test_coherent.cpp`, JSON -> loader -> Tracer): a tilted Michelson (alpha 0.5 mrad,
632.8 nm) gives dark fringes at 2.3181, 2.9508, 3.5835, 4.2166, 4.8492, 5.4818 mm - period
0.632755 mm against lambda / sin 2 alpha = 0.6328 mm - visibility 0.996, total 0.489 of 0.5 W; arms
0.2 m apart keep visibility 0.9988 fully coherent and fall to 0.0066 at Lc = 1 cm; one beam reads its
incoherent power to 1e-9; a random laser is refused at load. In the app, QA 10 shows straight
fringes across the beam, 2.47 mW on target, peak 442 W/m2 against 4 x 99.5 predicted.

**Two things measured before they were believed, both in the TEST, not the engine:** the first
visibility read 0.85 and the first period 0.606-0.621 mm. A bin-by-bin dump of arms showed the
physics right (dark bins 0.4% of incoherent, bright exactly 4x one arm, equal arm powers). The test's
"lit region" filter had dropped the dark fringes themselves, and then a partial-overlap dip at the
beam edge was fitted as a fringe. A bin-centre phase correction added on a guess in between changed
nothing measurable at 12 bins per fringe; it is kept as the physically right sampling and its comment
says so. Also: doctest `Approx(x).epsilon(e)` adds an absolute scale of 1, so on sub-millimetre
numbers it passes anything - use an explicit relative check.
- [ ] Stage 7 - user feedback, deliberately after Stage 6 because the coherent receiver changes what the
  flux map shows and Stages 5-6 give rays the path and phase an inspector should show:
  - [ ] Flux map on a FIXED scale taken from the source, chosen by the user over "lock on a trace" and
    "first trace sets it": 100% = total source power / receiver area (the flux if all the light fell
    evenly on the screen), so a weaker design visibly dims and a focused spot can exceed 100%. A
    sensitivity slider rescales the display; the scale never adapts to the trace.
  - [ ] Rays show wavelength (colour) and polarisation (a marker), and a clicked ray reports its
    wavelength, power, polarisation state, optical path and phase.

**Timing baseline (Stage 0), release, 98-scene corpus, summed `wall_time_s`:** 20.59 s (end of
Wave 4), 21.10 s and 20.86 s (two captures at Stage 0) - a spread of about +-1.2%, dominated by one
12 s scene. The ray-size decision in Stage 2 compares against these three.

**Stage 1 found the sign convention by testing it.** The existing `rp = (n2 ci - n1 ct)/(n2 ci +
n1 ct)` gives **rp = -rs at normal incidence** - not the rs = rp convention the first draft of the
header claimed. The bit-identity sweep (5000+ angle/index pairs, `==` against the pre-Wave-5 body
kept verbatim in `tests/test_fresnel.cpp`) passed; the corpus after Stage 1 is identical.

**QA scenes (`examples/feature_checks/qa_*.json`), asked for by the user to examine by hand.** Every
one was traced (release `scrt_compare`) and its number is in its name; the Scene Browser lists the
folder as "QA / ...". Two were WRONG on first trace and fixed, and are the reason to trace every
QA scene before handing it over:
- `rotation_euler_deg` is composed `Rx * Ry * Rz`, so a DIRECTION is turned by Z first, then Y,
  then X. A 45-degree splitter whose height stays vertical is `[90, 45, 0]`; `[90, 0, 45]` left the
  coating parallel to the beam and the "50:50" screen read the full 5 mW.
- The Sellmeier preset is `n_sf11`, not `sf11`; the loader refused the scene outright.
- QA 02's name first promised "a green spot inside blue/red blur". The flux map is not per colour
  and rays draw in one colour, so nothing on screen could show that. The foci were MEASURED
  instead (screen-position scan per colour: blue 168.0, green 167.0, red 166.5 mm, blue nearest
  the lens as it must be) and the name says how to check them.

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

### The library tab (Wave 3 Stage 6)

A fourth tab in the workspace column: **Components**, **Materials**, **Light sources**.
`src/viz/Library.cpp` holds the entries as data; `src/viz/panels/LibraryPanel.cpp` draws them.
Clicking a component adds the material it needs (when the scene lacks it) and then the element,
both through the Viewer's existing deferred queue, which `ElementOp` grew two cases for.

**Every row must behave as its name says, and `tests/test_library.cpp` is what enforces it.**
It parses each material entry in strict mode, builds it, drops EVERY component into a real
`SceneEditor` scene, and requires the result to survive a strict save and reload. A row with an
invented key or an impossible lens would fail there rather than on a user's click. The entries
the engine cannot honour are absent by design: a polarising beam splitter, a dichroic, a
ground-glass (transmissive) diffuser and a brushed-metal lobe. Gold and the dielectric laser
mirror ARE shipped, with their band or design wavelength in the name, because one number is
honest there and nowhere else.

User entries live in `%APPDATA%\solar-cooker-rt\materials.json`, beside the AI config. The
panel both reads and WRITES it ("Keep this scene's materials in my library"); shipping only the
read path would have left half the feature dead, and the round trip is in the test.

**`kLeftWidth` went 360 -> 390 in `Layout.hpp`.** A fourth tab did not fit: ImGui clipped every
label to "Scen...", "Libra..." and showed a tooltip with the full name. Found by screenshot, not
by reading - the four-tab bar looks fine in source.

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

### Gating policy from Wave 4 (agreed with the user, to cut run cost)

Wave 3 ran the 96-scene corpus at every stage - eleven times, three of those scenes at ten
million rays each - and it never once moved. That is the single most expensive habit in this
project, and most of it was buying nothing. From Wave 4:

| Gate | When |
|---|---|
| The five golden flux values | EVERY stage. Seconds, and they are the thing that matters. |
| Full `ctest` | Every stage. |
| 96-scene `scrt_compare` corpus diff | ONCE per wave, at the end - **and immediately** after any change to `Tracer`, to a surface's `intersect`, or to a material's `interact`, whatever stage that lands in. |
| Release build | Only when the corpus runs. Debug is enough for tests and for the screenshot harness. |
| Independent cold audit (subagent) | Only for a wave that touches the tracer, the schema, or thread-safety. |

Screenshots are NOT reduced. For a visual wave they are the gate, and `--headless` proves
nothing about the interface.

Cheap habits that are worth keeping: batch independent tool calls into one message, do not
re-read a file just written, and do not re-read `CLAUDE.md` (the harness injects it).

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
