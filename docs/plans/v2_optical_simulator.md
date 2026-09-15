# v2: from solar cooker to optical simulator

## Context

The app track (Waves 0–6) and the GUI redesign (Phases 1–6) are finished and shipped as v1.8.0.
The first rounds of real human testing produced a list of seven requests, plus one raised
mid-session, and together they change what the application *is*: a solar cooker ray tracer becomes
a general optical simulator that also does solar cookers.

Requested:

1. Bench optics — lenses, lasers, beam splitters, lab equipment on an experimental table, with
   sliders for the properties that matter.
2. A material library — reflective, diffusive and others — plus user-defined custom materials.
3. API keys from several AI providers.
4. A library of component primitives to drop into a scene and arrange.
5. An optimiser for receiver placement — maximum power, or clearest image.
6. A 3D grid in the viewport with anchor points for placement, if it is cheap enough.
7. Elements that look like the things they represent, materials included.
8. (raised mid-session) Expose the app's workings to the assistant that builds scenes.

Confirmed in discussion: **fully general, no modes** — the sun becomes one source among several
rather than a special case. **Both interference and diffraction**, with diffraction as a second
engine. **Plan it all, ship in waves.** The assistant gets **tools to edit the live scene**.
The optimiser offers every combination as a separate choice: receiver alone or any selected
element, maximising power or minimising spot size.

### What reconnaissance found that changes the starting point

Three findings from reading the code, in order of how much they matter:

- **An element added at runtime is never drawn.** `ctx.add_element` sets `need_rebuild_`, which is
  consumed only by `Scene::build_acceleration_structure` (`Viewer.cpp:227-230`). Nothing
  re-registers Polyscope structures, nothing adds an `ObjectEditState`. So an imported mesh is
  traced and saved but is invisible, unselectable and un-placeable. `ImportPanel.cpp:348-352`
  claims otherwise and is wrong. **Items 1, 4 and 8 all depend on dropping things into a scene, so
  this is the foundation, not a detail.**
- **Material edits never reach the document.** `MaterialsPanel` writes the live `Material` object
  only; nothing writes `editor->doc().materials`. Change a reflectance, save, and the *old* value
  is written out. The same is true of the Sun panel and of `TraceConfig`. This is precisely the bug
  class fixed for transforms in GUI Phase 1, in three more places.
- **`SceneEditor::remove_element`, `duplicate_element` and `rebuild_element` are implemented and
  tested, and no code calls them.** A component library needs all three.

And two that constrain the physics work:

- **The solar footprint of the tracer is four lines, duplicated twice** (`Tracer.cpp:120-126`,
  `:241-247`) plus two `sun.sample_ray(ap, …)` calls. Everything below `trace_one` is already
  general. Generalising is a smaller job than it sounds — but the *contract*
  `sample_ray(const Aperture&, Rng&)` puts the aperture in the source's interface, so no source can
  originate its own rays, and per-ray power is `DNI × area × cos / N` stamped over whatever the
  source returned. A laser has watts, not an irradiance.
- **`InteractionKind::Split` already does deterministic power splitting** and the tracer already
  handles it — beam splitters have a substrate. But the reflected branch is traced by a *recursive*
  call with a **fresh `max_bounces` budget** and writes into the **same path vector** as its
  parent, so recorded split paths render as one polyline that teleports between branches. Incidental
  today; central once beam splitters exist.

### Intended outcome

An optical simulator whose engine makes no assumption about the sun, in which a laser bench and a
solar cooker are two scenes rather than two modes; which models polarisation, interference and
diffraction and says plainly which of them is in play; which ships libraries of components and
materials a user can extend; and whose assistant can see the scene and change it.

---

## Standing constraints

- **The four golden flux values must not move.** Every wave ends with them green. Where a change
  could move them, the plan says so explicitly and how it is avoided.
- **Every shipped scene file must keep loading**, including the regression corpus. Schema changes
  are additive; `"sun": {…}` keeps working untouched.
- **The document is authoritative for structure** (`SceneEditor.hpp:17-56`). New mutators go on
  `SceneEditor`, never on `Scene` directly.
- **`SceneWriter`'s `overloaded` visitor is a compile-time tripwire** — a new `SurfaceDoc`
  alternative that nobody serialises fails to build. Keep it that way.
- **Each wave ends in a build you can run**, tests green, packaged if it is worth looking at.

---

## Wave 1 — Foundations: make the scene actually mutable

Nothing else on the list works until an object added at runtime appears, can be picked, moved,
and saved with its edits.

**Files:** `src/viz/Viewer.cpp`, `include/scrt/viz/Viewer.hpp`, `include/scrt/viz/Panels.hpp`,
`src/viz/RayRenderer.cpp`, `src/viz/panels/MaterialsPanel.cpp`, `src/viz/panels/SunPanel.cpp`,
`src/scene/SceneEditor.cpp`, `include/scrt/scene/SceneEditor.hpp`, `src/io/SceneLoader.cpp`,
`src/tracer/Tracer.cpp`.

- **Element lifecycle.** One `Viewer` path that, given an element id, registers or re-registers its
  Polyscope structure, seeds its `ObjectEditState`, and refreshes the outliner row table — and the
  inverse on removal. `RayRenderer::reregister_surface` and `remove_surface_structure` already
  exist and have no callers; this is their wiring. Expose `remove_element` and `duplicate_element`
  through `PanelContext` and put Delete and Duplicate in the outliner.
- **Edits reach the document.** `SceneEditor::add_material` and `update_material`; the materials
  panel, the sun panel and the trace settings write through the editor rather than at the live
  objects. Pin with a test in the shape of `"a committed placement survives save and reload"`.
- **De-duplicate `build_surface`.** `SceneEditor.cpp:51-84` is a deliberate copy of the file-local
  one in `SceneLoader.cpp:85-116`, with a comment asking for exactly this. Promote one to a public
  `io::` helper and delete the copy — a new surface type otherwise has to be added in two places.
- **Fix the `Split` defect — in two separate commits, because only one half is safe.**
  - *The recorded path becomes a tree* (nodes + edges + per-edge power, replacing the flat
    `vector<vec3>`), so a split draws as a branch rather than a polyline that teleports.
    **Provably flux-neutral:** `path` is write-only inside `trace_one` and never feeds back into a
    traced ray, and every golden run sets `record_paths = false`. Verify anyway.
  - *The branch stops getting a fresh bounce budget.* **This one may move a golden value** and
    whether it does cannot be settled by reading — `fresnel_lens_cooker.json` has `max_bounces: 8`,
    a dielectric that splits at every surface, and a `1e-9` power cutoff, so whether any path today
    exceeds 8 *total* bounces is an empirical question. **Measure first** (instrument `trace_one`,
    count max total depth on the golden scenes, throw the instrumentation away). If it moves, that
    is a genuine physics bug fix and the baseline must be re-recorded — but as a standalone commit
    carrying the before/after and the measured cause, and **escalated as a decision, not absorbed**.
  - *The ordering trap in that rewrite:* replacing the recursion with a stack must **push the
    transmitted ray and continue inline with the reflected one**. Today the reflected sub-tree is
    traced to completion before the transmitted tail resumes, so every RNG draw in the reflected
    branch precedes every draw in the tail. Get it backwards and the RNG sequence shifts and every
    dielectric scene moves — silently, and it would be misattributed to the budget change. Land the
    recursion→stack rewrite *keeping the old fresh budget* first: it must be bit-identical, which
    is how you find out immediately if the ordering is wrong.
  - `trace_one` exists twice with near-identical bodies; both copies carry both bugs. Collapse to
    one file-local template parameterised on a deposit policy, or the two will drift.

**Ships:** imported and assistant-generated objects appear and can be placed; deleting and
duplicating work; material and sun edits survive a save.

---

## Wave 2 — General sources: the sun stops being special

The pivot the rest of the physics hangs off. A detailed design for this wave exists separately;
its shape is fixed by two decisions: **a source originates its own rays and sets its own per-ray
power**, and **the sun keeps its `DNI × area × cos` normalisation by owning its aperture** rather
than by having the tracer apply it.

**Files:** `include/scrt/sources/*`, `src/sources/*`, `src/tracer/Tracer.cpp`,
`include/scrt/scene/Scene.hpp`, `src/scene/Scene.cpp`, `src/io/SceneDocument.cpp`,
`src/io/SceneWriter.cpp`, `src/io/SceneLoader.cpp`, `src/viz/panels/SunPanel.cpp`.

- Replace `SunSource`'s aperture-bound `sample_ray(const Aperture&, Rng&)` with a source interface
  that returns a fully-formed ray, power included. The sun subclass keeps DNI, direction,
  azimuth/elevation and the aperture; a laser subclass has origin, direction, total watts,
  wavelength, beam diameter and divergence.
- Aperture becomes optional on `Scene` (it is held **by value** today, so every scene has one
  whether or not it means anything), and the GUI stops drawing a disk for scenes without one.
- Sources become a list rather than a single `unique_ptr`, with primary rays divided by power
  share. Deterministic per-slot seeding must survive.
- **Wavelength:** sources set `Ray::wavelength_nm`, which `Dielectric` already reads for Sellmeier
  dispersion and for a spectral absorption curve. The sun keeps setting 550 nm.
- Schema: `"sun"` keeps parsing unchanged; a new `"sources"` array is the general form. Both keys
  present is a hard error in strict *and* lax mode — picking a winner silently drops an authored
  source, and this repo's history says silent-drop bugs cost more than a loud one.
- **The writer stays legacy-preferring:** one sun and nothing else re-emits `"sun"` + `"aperture"`,
  not `"sources"`. That is what makes all 94 shipped scenes re-save byte-identically, and what means
  the assistant's system prompt and `test_assistant_schema.cpp` need no change at all — otherwise
  scene generation breaks silently for every user with an API key.
- The aperture moves **onto the sun** rather than becoming optional on `Scene`. An
  `optional<Aperture>` on the scene plus an aperture on the source would be two copies of one fact.
  `Scene` keeps only a `display_aperture()` accessor for the GUI disk, marked presentation-only.

**Two risks to name now.**

This wave rewrites the one code path every existing result depends on. The behaviour-preserving
proof is the regression corpus plus the four golden values, run before and after on identical seeds.

But **the golden values will not catch a wavelength regression**, which is worth knowing before
leaning on them. The only golden scene with a dielectric is `fresnel_lens_cooker.json`, and it
specifies a constant `"n": 1.5` and a constant `absorption_per_m` — no Sellmeier preset, no
`alpha_spectrum`. `Dielectric` consults the wavelength only when one of those is present, so all
four goldens are insulated from a wavelength change **by accident, not by design**. A source that
started emitting 1064 nm would pass every golden check while quietly changing results for any user
scene using `"sellmeier": "bk7"`. Wave 2 needs its own test for this: a Sellmeier scene traced at
two wavelengths, asserting the index and the outcome differ by what Sellmeier says they should.

**Ships:** a laser scene that traces; every solar scene unchanged to the last digit.

---

## Wave 3 — Components and materials, as libraries

**Files:** new `include/scrt/surfaces/ThickLens.hpp`, `SphericalMirror.hpp`;
new `include/scrt/materials/BeamSplitter.hpp`, `Diffuser.hpp`; `src/io/SceneDocument.cpp`,
`src/io/SceneWriter.cpp`, `src/io/SceneLoader.cpp`; new `src/viz/panels/LibraryPanel.cpp`;
new `src/viz/MaterialLibrary.cpp`.

- **A real lens.** Today the only lens-like surface is `FresnelZoneLens`, which is a *flat plane
  with fictitious normals* — one refracting event at zero thickness. A `ThickLens` is two curved
  interfaces separated by glass, which the engine can trace because `Dielectric` infers the medium
  from `h.front_face`. Note the limit this exposes: `Scene::intersect` has no notion of which
  medium a ray is currently inside, so two overlapping dielectric bodies are undefined — document
  it and keep bodies disjoint.
- **A beam splitter material** with a *designed* split ratio. Every split ratio today is derived
  from Fresnel, so a 50:50 splitter can only be faked by solving for an index that gives R=0.5 at
  one angle and drifts everywhere else. `InteractionKind::Split` already carries both branches.
- **A diffuser** — the first material that scatters rather than reflecting specularly. `RealMirror`
  perturbs a normal; a diffuser needs a real BRDF sample. This is new physics, not a parameter.
- **Material library:** named presets (aluminised foil, silvered glass, BK7, fused silica, black
  anodised, white diffuser, …) plus user-defined entries, persisted next to the AI config in
  `%APPDATA%\solar-cooker-rt\`. Depends on `SceneEditor::add_material` from Wave 1.
- **Component library:** a catalogue panel of ready-made elements — mirror, lens, beam splitter,
  laser, screen, iris, table — each an `ElementDoc` template dropped at a chosen point. Depends on
  the element lifecycle from Wave 1.
- **Optical table:** a scene template plus a table component. Z-up stays; the bench is a horizontal
  table with beams travelling horizontally, not a new coordinate system.

**Ships:** open a bench template, drop a laser, a lens and a screen, pick materials, trace it.

---

## Wave 4 — Look like the real thing, and place things precisely

**Files:** `src/surfaces/*.cpp` (`tessellate`), `src/viz/RayRenderer.cpp`,
`src/viz/panels/TransformPanel.cpp`, `src/viz/ViewSettings.cpp`, `src/viz/panels/SettingsPanel.cpp`.

- **Representative geometry, generated procedurally** from the same parameters the physics uses, so
  the picture and the optics cannot disagree. A lens gets real thickness and a rim; a mirror a
  substrate; the table legs. Every `tessellate` emits **world-space** vertices and the renderer
  caches a baked inverse — new geometry must follow that or placement breaks.
- **Material-driven appearance.** `setMaterial` is *never called anywhere in the project* today,
  and Polyscope ships matcaps (`clay`, `wax`, `candy`, `ceramic`, `jade`, `flat`, `mud`, `normal`).
  Glass gets `wax`/`candy` plus transparency and `BackFacePolicy::Identical`; mirrors a metallic
  finish; absorbers matte. The current rule is string-matching on names for `"glass"`/`"pmma"`
  (`RayRenderer.cpp:44`) — replace it with the material type.
- **Grid and snapping.** `ImGuizmo::Manipulate` already takes a `snap` argument that the app passes
  as `NULL` (`TransformPanel.cpp:287-289`); translate uses `snap[3]`, rotate and scale use `snap[0]`
  only. That is the whole snapping job. The grid itself is our own geometry, not `ImGuizmo::DrawGrid`
  — that draws Y-up into an ImGui draw list with no depth test, and this project is Z-up.
- **The trap to respect:** any structure registered with Polyscope is folded into the global
  `lengthScale` and `boundingBox`, and `hasExtents()` is not overridable for a `SurfaceMesh` or
  `CurveNetwork`. A decorative grid or table will resize the ground plane and every "relative"
  length unless extents are pinned. Three separate bug comments in the codebase record what that
  costs.

**Ships:** a bench that looks like a bench, and objects that snap to a grid.

---

## Wave 5 — Polarisation and interference

**Files:** `include/scrt/core/Ray.hpp`, `include/scrt/optics/*`, every `src/materials/*.cpp`,
`include/scrt/tracer/FluxAccumulator.hpp`, `src/tracer/Tracer.cpp`, new polariser and waveplate
materials.

- **Polarisation:** a Jones vector per ray in an s/p basis plus the basis itself, and
  polarisation-aware Fresnel replacing `fresnel_unpolarized`. Makes polarisers, waveplates and
  polarising beam splitters real rather than approximated.
- **Interference:** optical path length accumulated per ray, a phase from
  `2π·OPL/λ`, and a **coherent** accumulator mode that sums complex amplitude per bin and takes
  `|Σa|²` instead of `Σ|a|²`. Complex addition is associative, so the per-thread merge still works.
- **Coherence length** on the source: rays whose path difference exceeds it add incoherently. Without
  this every source behaves like an ideal laser and fringes appear where they should not.
- **The pitfall that decides whether this works at all:** random Monte Carlo sampling summed
  coherently produces *speckle*, not fringes. A coherent run needs deterministic, stratified
  sampling over the source aperture rather than the random sampling the solar path uses. The laser
  source must offer both, and the UI must not let a coherent detector run on a randomly-sampled
  source without saying what it is looking at.
- **Cost:** `Ray` roughly doubles in size, and a Monte Carlo tracer is memory-bound. Measure the
  regression corpus before and after; only reach for a templated ray payload if the measurement
  justifies it.

**Ships:** a Michelson interferometer with real fringes; a prism that disperses; crossed polarisers
that go dark.

---

## Wave 6 — Diffraction, as a second engine

Rays travel in straight lines and do not bend around an edge; no amount of ray tracing produces a
single-slit pattern. This wave adds a scalar wave-propagation module that the ray tracer hands off
to at a chosen plane.

**Files:** new `include/scrt/wave/*`, `src/wave/*`; `vcpkg.json`; `src/viz/panels/` for the view.

- Sample the complex field on a hand-off plane from the rays arriving at it, then propagate with an
  angular-spectrum or Fresnel integral to the detector.
- **FFT dependency:** pocketfft or kissfft (both permissively licensed and small). **Not FFTW** —
  its licence is GPL-or-commercial and this app ships as a binary. `vcpkg.json` is owned by the
  wave integrator, not by the workstream.
- Sampling and aliasing are the whole difficulty: the grid pitch sets the maximum angle that can be
  represented, and an undersampled field produces confident nonsense. The panel must show the
  sampling limit alongside the result.

**Ships:** a single slit, a double slit and a circular aperture that produce the right patterns,
with the sampling limits stated.

---

## Wave 7 — The optimiser

**Files:** new `include/scrt/opt/*`, `src/opt/*`; `src/scene/SceneEditor.cpp`;
new `src/viz/panels/OptimisePanel.cpp`.

- **The missing API first:** `SceneEditor` cannot move the receiver at all today — `commit_transform`
  works only on surface ids, and receiver faces are registered with `surface_id = 0`. An optimiser
  that "moves the receiver" has no runtime API to call.
- **Variables:** receiver position alone; or the position and rotation of any elements selected in
  the outliner. Both offered as separate choices.
- **Objectives:** maximum total power; minimum spot size; maximum peak flux. Reported together so
  the user can see the trade.
- **Search:** coordinate descent to start — it is what `scripts/optimize_compact.py` already does
  well in Python, and porting its staged structure is cheaper than inventing one. Pattern search or
  Nelder–Mead if measurement says the staged version stalls.
- **In-process, not process-per-scene.** Every existing sweep (`scripts/sweep.py`,
  `optimize_compact.py`) writes JSON files and shells out to `scrt_compare`. An in-app optimiser
  runs many traces over a mutated in-memory scene — a path that does not exist yet. It must reuse
  `TraceControl` so it can be cancelled, and report progress per iteration.

**Ships:** select the receiver, choose an objective, watch it converge, keep or discard the result.

---

## Wave 8 — The assistant, with tools

**Files:** `src/viz/ClaudeClient.cpp` → a provider abstraction; `src/viz/AppConfig.cpp`;
`src/viz/panels/AiOverlay.cpp`; new tool-dispatch module; `tests/test_assistant_schema.cpp`.

- **Multiple providers.** `send_claude_request` is the single seam and `AppConfig` the single
  persistence struct, but the transport hard-wires `api.anthropic.com`, the `/v1/messages` path,
  the `x-api-key` header (not `Authorization: Bearer`), a top-level `system` field,
  `output_config.effort`, and Anthropic's `stop_reason` vocabulary. A provider interface with one
  implementation per vendor, and a key per provider in the config.
- **Tools over the live scene.** The assistant gets add / move / set-material / remove / run-trace /
  read-result and works in a loop. This shares its mutation API with Wave 7 and its element
  lifecycle with Wave 1 — which is why it comes last. It needs a visible transcript, a step limit,
  and an undo that restores the document, because the model is changing the user's work and
  spending their tokens in a loop.
- **Generate the schema from the code.** The system prompt is a hand-written contract with
  `parse_document`, and it was wrong on its first run — it told the model an absorber takes a
  `reflectance`, which strict mode rejects, so every generated scene would have failed to load.
  `tests/test_assistant_schema.cpp` catches that after the fact; generating the surface and
  material tables from the same source the parser uses would stop it happening.

**Ships:** "make the reflectors 10 cm wider and re-trace" as a conversation with the scene.

---

## Verification

Per wave, before it is called done:

- `cmake --build --preset debug` clean, then
  `ctest --test-dir build/debug --output-on-failure` — all cases green, **the four golden flux
  values unchanged**.
- Launch the app and confirm it holds a window. `--headless` does not construct the viewer, so it
  proves nothing about the GUI; `scripts/screenshot_app.ps1` photographs the real window and can
  click into it first.
- The regression corpus traced before and after any engine change, on identical seeds, compared
  numerically — not by eye.
- `scripts/package.ps1 -Version X.Y.Z`, and the packaged build launched from its own directory.

Wave-specific:

- **Wave 2:** every example scene and every corpus scene loads and produces identical numbers.
- **Wave 5:** a Michelson with a known path difference produces the analytically expected fringe
  spacing; crossed polarisers extinguish; a prism's deviation matches Snell at the stated index.
- **Wave 6:** a single slit matches the analytic sinc²; a circular aperture matches the Airy
  pattern's first-null radius.
- **Wave 7:** on a scene whose best receiver position is known analytically, the optimiser finds it.

---

## Sequencing note

The order is set by dependency, not by the numbering of the original requests. Wave 1 unblocks
items 1, 4 and 8; Wave 2 unblocks all the physics; Waves 7 and 8 share a mutation API that does not
exist until Wave 1. The optimiser and the multi-provider AI keys have no physics dependency and can
be pulled forward if they matter more than the bench does.
