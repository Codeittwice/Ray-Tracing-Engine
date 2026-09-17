# Waves 4–8 — design

Design detail for the remaining waves of `v2_optical_simulator.md`. Waves 1 and 2 are complete and
have their own records (`CLAUDE.md`, `v2_wave2_sources_design.md`); Wave 3's payload is in
`v2_wave3_library_contents.md`. This covers 4 through 8.

Written against the code at Wave 2 complete. Where a claim is load-bearing it names the file and
line it was checked at — re-check before relying on it, because this document will age.

---

## Standing rules, restated because every wave below can break them

- **The five golden flux values must not move.** Where a change *could* move one, the design says
  how that is avoided and what asserts it. Use `==`, not `Approx`, for bit-identity claims.
- **`scrt_compare` over the corpus is the real gate**, not the five goldens. It writes 17
  significant digits since Wave 2. Capture a baseline CSV before starting a wave and diff every
  column but `wall_time_s`. **Once per wave from Wave 4 onward** (it never moved once in six
  Wave 3 stages, and three of its scenes trace ten million rays) — plus immediately after any
  change to `Tracer`, to a surface's `intersect`, or to a material's `interact`. The five
  goldens stay a per-stage gate because they cost seconds. See CLAUDE.md for the full table.
- **`--headless` does not construct the viewer.** `scripts/screenshot_app.ps1` is the only check
  that looks at a pixel; it can click into the window first with `-Clicks`.
- **Ship what the engine can honour.** A control or library row that behaves unlike its name is
  worse than a missing one.

---

# Wave 4 — representative geometry, grid and snapping

**Goal:** a lens looks like a lens, a mirror like a mirror, a bench like a bench; and objects can
be placed precisely instead of dragged by eye.

## 4.1 What exists

- `Surface::tessellate(nseg, verts, indices)` is the only visualisation hook, and **every
  implementation emits WORLD-space vertices** — each calls `xform_.point_to_world(...)` inside its
  loop. There is no local-space path.
- `RayRenderer::register_surfaces` interns a name, records `set_baked(id, world)` (which stores
  `glm::inverse(world)`), registers the mesh, then `resetTransform()`. A later placement edit only
  applies the *difference*: `display = world * baked_inverse(id)`.
- `tess_segs` is hardcoded `32` at the single call site (`Viewer.cpp`, `register_scene`).
- `ImplicitSDF::tessellate` is an empty stub, so those surfaces are invisible. Nothing constructs
  one from JSON, so this is latent rather than live.
- **`setMaterial` is never called anywhere in the project.** The only appearance rule is
  `is_transparent_lid_surface` in `RayRenderer.cpp`, which **string-matches the surface's or
  material's name for "glass" or "pmma"** and applies a blue tint plus transparency.

**New geometry must emit world-space vertices and be registered through the same baked-transform
path, or placement silently breaks.**

## 4.2 Procedural geometry

Generate from the same parameters the physics uses, so the picture and the optics cannot disagree.

| Component | Drawn as |
|---|---|
| Thick lens | Two spherical caps joined by a cylindrical rim, from `radius1`, `radius2`, `center_thickness`, `diameter`. A real body with a real edge. |
| Mirror | The reflective surface plus a substrate slab behind it and a rim, so it reads as an object rather than an infinitely thin sheet. |
| Beamsplitter cube | Two prisms with a visible diagonal. |
| Optical table | A slab plus a hole grid (see 4.4) plus legs. |
| Post / mount | A cylinder and a simple mount body. |

**The honesty constraint:** the drawn body may be more detailed than the surface actually traced —
a lens rim is drawn but no ray interacts with it. That is acceptable and normal, but it must be
stated somewhere the user can find, because "it looks like a real lens" invites the belief that
every part of it is simulated.

## 4.3 Material-driven appearance

Polyscope ships matcap materials — `clay`, `wax`, `candy`, `flat`, `mud`, `ceramic`, `jade`,
`normal` (`Engine::loadDefaultMaterials`) — and `SurfaceMesh` exposes `setMaterial`,
`setBackFaceColor`, `setBackFacePolicy`, `setShadeStyle`, `setTransparency`. None is used today.

Replace the name string-match with dispatch on the **material type**:

| Material type | Appearance |
|---|---|
| `perfect_mirror`, `real_mirror` | `candy` or `wax`, near-white tint, `setEdgeWidth(0)`, smooth shading. |
| `dielectric`, `thin_dielectric_pane` | `wax`, pale blue tint, `setTransparency(~0.25)`, `BackFacePolicy::Identical` so the far wall of a glass body is not painted the inverse colour. |
| `absorber` | `clay`, near-black, matte. |
| `diffuser` (Wave 3) | `clay`, white, matte. |
| `beam_splitter` (Wave 3) | `wax`, pale, semi-transparent. |

`setShadeStyle(MeshShadeStyle::Smooth)` matters for curved bodies — the default is `Flat`, which
makes a 32-segment lens look faceted.

## 4.4 The grid, and the trap that governs it

**Do not use `ImGuizmo::DrawGrid`.** It draws in the **y = 0 plane** (this project is Z-up
throughout) into an ImGui draw list, so it has **no depth test** and would paint over the scene.

Draw the grid as our own geometry — a `CurveNetwork` of line segments, or a `SurfaceMesh` for a
table top with holes.

**The trap:** any structure registered with Polyscope is folded into the global `state::lengthScale`
and `state::boundingBox` by `updateStructureExtents()`, and `Structure::hasExtents()` is **not
overridable** for a `SurfaceMesh` or `CurveNetwork` — only `FloatingQuantityStructure` overrides it.
So a decorative grid or table **will resize the ground plane and every "relative" length in the
scene**. Three separate bug comments in this codebase record what that costs (the ground plane
tracking the bounding box, the ray radius becoming "fat sausages", the whole-scene flash while
scaling).

Two workable answers, in order of preference:

1. **Size the decoration from the scene** rather than the other way round — a grid that spans the
   existing bounds adds nothing to them.
2. **Pin `options::automaticallyComputeSceneExtents = false`** around registration and set the
   extents explicitly. Heavier, and it fights the deliberate comment in `TransformPanel.cpp` that
   the freeze is scoped to a drag *because registration and import still need automatic sizing*.

Also: **decorative structures are pickable.** `sync_from_pick` (`OutlinerPanel.cpp`) already
guards — an unknown structure name falls through and is ignored — but check that a click on the
table does not clear the selection through the empty-pick path. If it does, exclude decoration by
name before the empty test.

## 4.5 Snapping — the whole job is one argument

`ImGuizmo::Manipulate` takes a `snap` parameter the app currently passes as `NULL`
(`TransformPanel.cpp`, the five-argument call). Semantics, from ImGuizmo's source:

- **Translate** uses `snap[3]` — three independent metre steps, applied to the cumulative delta
  from drag start, rotated into the source frame first in LOCAL mode.
- **Rotate** uses **`snap[0]` only, in degrees.**
- **Scale** uses **`snap[0]` only, broadcast to all three axes.**
- `ComputeSnap` has a hysteresis band (`snapTension = 0.5`), so it does not jitter at the midpoint.

Add a snap toggle and three step values (translate m / rotate deg / scale factor) to the transform
panel, and apply the same snap to the numeric fields so the two input paths still cannot disagree —
which is the existing invariant documented on `ObjectEditState`.

## 4.6 A picture of every library entry

Wave 3 shipped a library of 24 components and 31 materials as a wall of text buttons. A user
choosing between "Plano-convex lens, f = 50 mm" and "Bi-convex lens, f = 50 mm", or between
"Matte white paint" and "Spectralon white standard", is reading names for information that is
inherently visual. Each entry gets a small picture.

**The governing rule is 4.2's, applied to the panel: the picture is GENERATED FROM THE SAME
PARAMETERS AND THE SAME CODE the physics uses, so it cannot disagree with what a trace does.**
No hand-drawn icons and no PNG assets: an icon drawn by hand goes stale the moment a radius
changes, and it goes stale silently.

### 4.6.1 Component thumbnails — a real render of the real geometry

Not a per-type icon. One path that works for every surface, including ones added later:

1. Build the entry's surface with `io::build_surface`, at identity transform.
2. `tessellate(nseg, verts, indices)` — every surface already implements it, and it already
   emits world-space vertices, which at identity IS local space.
3. Project orthographically from a fixed three-quarter view (azimuth ~35 degrees, elevation ~25),
   scaled to fit the thumbnail from the surface's own `local_bounds()`.
4. Flat-shade each triangle from its facet normal against one fixed light direction, sort by
   centroid depth (painter's algorithm), and emit `AddTriangleFilled` into the ImGui draw list.

That is a genuine little 3D render of the actual traced geometry, in about forty lines, with no
render target, no GPU work beyond ImGui's own, and no per-type code to keep in step.

**Four things to get right, each of which has bitten this project before:**

- **Never register these with Polyscope.** A thumbnail mesh folded into `state::lengthScale`
  and `state::boundingBox` would resize the ground plane and every relative length in the
  scene — the trap 4.4 is about. These are draw-list triangles and nothing else.
- **Cache per entry, once.** Library entries never change during a session, so the projected
  triangles and their colours are computed on first draw and kept. A solar dish tessellates to
  a couple of thousand triangles at `nseg = 32`; thumbnails should use `nseg` around 10 to 14,
  which is plenty at 48 pixels and keeps the cache small.
- **Painter's algorithm has no depth buffer.** Centroid sorting is right for the convex-ish
  bodies here (lens, disk, sphere, dish) and wrong for self-occluding ones. If a shape reads
  badly, draw it as a silhouette outline rather than shipping a picture that misleads.
- **`ImplicitSDF::tessellate` is an empty stub**, so an SDF surface would render as nothing.
  Nothing constructs one from JSON today, so this is latent — but the thumbnail path must show
  a placeholder rather than an empty box, or the entry looks broken.

### 4.6.2 Material swatches — a picture of what the material DOES

A colour chip alone would say almost nothing: a 50:50 splitter and a 90:10 pickoff are the same
colour. Each material gets a small diagram of its interaction, **drawn by calling
`Material::interact()` itself** with a fixed incoming ray, a fixed-seed `Rng`, and a fixed
surface normal, then drawing the outgoing rays with line thickness (or opacity) proportional to
the power each carries.

| Material | What the diagram shows |
|---|---|
| `perfect_mirror`, `real_mirror` | One reflected arrow; its weight is the reflectance, so a foil mirror is visibly thinner than protected silver. `real_mirror`'s slope error shows as a small spread when several samples are drawn. |
| `dielectric` | A bent transmitted ray at the true Snell angle for that index, plus the weak Fresnel reflection. The bend is the parameter you are choosing. |
| `thin_dielectric_pane` | Transmitted ray UNBENT, plus the slab reflection — which makes the model's one real limitation visible rather than buried in a tooltip. |
| `beam_splitter` | Two arrows whose weights are R and T. A 50:50 and a 90:10 look obviously different. |
| `diffuser` | Forty sampled outgoing rays: the cosine fan, with overall opacity set by the albedo. Matte black and white card are the same shape at very different brightness, which is exactly the truth. |
| `absorber` | An incoming arrow and nothing leaving. |

Because the diagram is produced by the material's own `interact()`, a material whose behaviour
does not match its name is visible in the panel. That is the same rule `tests/test_library.cpp`
enforces at build time, now enforced where the user is looking.

Beside the diagram, the appearance tint from 4.3, so the swatch also previews how the object
will look in the 3D view.

### 4.6.3 Where else the previews earn their place

The same two widgets should be reused, not reimplemented:

- The **materials panel** (Design tab), beside each material's sliders, so dragging a
  reflectance or an albedo updates the diagram live. This is the strongest version of the
  feature: you see the physics change as you drag.
- The **outliner's** selection read-out, for the selected object's material.
- The **import panel**, to preview a mesh before committing it.

## 4.7 Drag and drop from the library into the scene

Wave 3's library drops a component at a point typed into three boxes. That is precise and it is
also the wrong first gesture: the natural one is to drag the entry and let go where you want it.
Keep both — typed coordinates are how you place something exactly, dragging is how you place it
roughly and then adjust.

**Both halves were checked to exist before this was planned**, because the viewport is not an
ImGui window and a plan that assumed otherwise would have been worthless:

- ImGui here is **1.90.4** (Polyscope's vendored copy), which has the full
  `BeginDragDropSource` / `BeginDragDropTarget` / `AcceptDragDropPayload` API.
- Polyscope exposes **`view::screenCoordsToWorldPosition`** (`view.h`), which queries the depth
  buffer, and `view::screenCoordsToWorldRay` beside it.

### 4.7.1 The target problem, and the shape of the answer

The 3D view is Polyscope's render surface with our ImGui windows floating over it. There is no
ImGui item covering it, so there is nothing for a payload to be dropped onto.

**Do not create a permanent invisible window over the viewport.** It would sit between the user
and Polyscope's camera handling and swallow rotate, pan and zoom — and edge-panning, which the
user has asked be preserved (open bug 3).

Instead, create that overlay **only while a drag is actually in flight** — `ImGui::
GetDragDropPayload()` returns non-null — and tear it down on the same frame the drag ends. A
borderless, background-less window over `Layout`'s `viewport_min`/`viewport_max` containing one
`InvisibleButton`, which is the drop target. No drag, no window, no interference.

This is also **the first consumer of `viewport_min`/`viewport_max`**, which `Layout.hpp` has
exposed since the GUI redesign with nothing reading it. Note carefully: that rect was added for
the edge-panning bug, and this is NOT that fix. Do not let one be reported as the other.

### 4.7.2 Where the object lands

In order, each falling through to the next:

1. **`view::screenCoordsToWorldPosition(mouse)`** — a depth-buffer query, so the object lands on
   whatever is under the cursor. Dropping a lens onto the optical table, or a mirror onto a dish,
   puts it there, which is the whole point of dragging rather than typing.
2. **Nothing under the cursor** (the depth query returns the far plane): intersect
   `screenCoordsToWorldRay` with the ground plane z = 0 and drop there.
3. **That fails too** (the ray is parallel to the ground or points at the sky): fall back to the
   typed placement point, and say so in the panel's status line rather than dropping the object
   somewhere arbitrary.

Apply 4.5's translate snap to the result, so a dragged object lands on the grid exactly as a
gizmo-dragged one does. The two input paths must not disagree — the same invariant
`ObjectEditState` already documents.

### 4.7.3 The one that will actually bite: coordinates

**ImGui mouse positions are in points; Polyscope's buffer coordinates are in pixels**, and on
this project's own high-DPI target those differ by the display scale — `view::bufferWidth`
against `view::windowWidth`. Get it wrong and objects land at a consistent fraction of the way
toward a corner, which looks like a plausible placement rather than like a bug.

So the gate is not "a drop works". It is: **drop a component onto a specific, visible feature of
an existing scene, at 100% scale and again at a scaled display, and confirm from the outliner's
read-out that its coordinates are the feature's.** Nothing short of that distinguishes a correct
transform from a scaled one.

Two smaller ones to get right:

- A payload released **outside** the viewport must be discarded cleanly, not dropped at the last
  point inside it.
- The drag must be **refused while a trace is running**, like every other scene mutation. The
  existing `busy` gate greys out the library buttons, but a drag already in flight when a trace
  starts needs its own check at the drop.

## 4.8 Gates

- Every golden unchanged (this wave should not touch the physics at all — if a golden moves,
  something is wrong with *how* geometry is being generated, not with appearance).
- Screenshot the bench before and after; a lens must read as a lens.
- Resize the window and confirm the grid does not change the ground plane height.
- Scale an object and confirm the ray radius does not change — the regression this wave is most
  likely to reintroduce.
- **Screenshot the library tab and LOOK at it.** Every component thumbnail must show the shape
  its name claims, and no entry may render empty. This is the gate for 4.6 and nothing else can
  stand in for it: the whole feature is whether a picture is right.
- **A thumbnail must not move the scene.** Open a scene, note the ground plane height and the
  ray radius, open the library tab, and confirm neither changed. This is the Polyscope extents
  trap, and a draw-list-only implementation passes it by construction — so the check is really
  asking whether the implementation stayed draw-list-only.
- Each material diagram checked against a number it can be checked against: the splitter's two
  arrow weights against its R and T, the dielectric's bend against Snell at its index, the
  diffuser's fan against Lambert. If a diagram cannot be checked against the material's own
  output, it should not be drawn.
- **Drag a component onto a named feature of a real scene and read back its coordinates** from
  the outliner, at normal and at scaled DPI. A points-versus-pixels mistake produces a
  plausible-looking wrong position, so only the numbers settle it.
- With no drag in flight, the camera still rotates, pans and zooms over the whole viewport: the
  drop overlay must not exist except during a drag.

---

# Wave 5 — polarisation and interference

**Goal:** polarisers, waveplates and polarising beamsplitters behave correctly, and an
interferometer produces real fringes.

## 5.1 The ray payload

`core::Ray` today is `{origin, direction, power, wavelength_nm, bounces, id}` — 48 bytes.

Add:

```cpp
    /// Jones vector in the (s, p) basis: complex amplitudes, |Es|^2 + |Ep|^2 == 1 for a
    /// fully polarised ray. Unpolarised light cannot be a single Jones vector; see 5.3.
    std::complex<double> Es{1.0, 0.0}, Ep{0.0, 0.0};
    /// The s-axis this Jones vector is expressed in: a unit vector perpendicular to `direction`.
    /// Every interaction must rotate the vector into the new interface's s/p frame before
    /// applying Fresnel, and this is what makes that possible.
    math::vec3 s_axis{0.0, 1.0, 0.0};
    /// Accumulated optical path length [m] — geometric distance times the medium index.
    /// Phase is 2*pi*opl / wavelength.
    double opl_m{0.0};
```

That roughly doubles `Ray`. A Monte Carlo tracer is memory-bound, so **measure the regression
corpus before and after**; only reach for a templated ray payload if the measurement justifies it.
Predicting it is not the same as measuring it.

## 5.2 Polarisation-aware Fresnel — and why the goldens will not move

Today (`src/optics/Fresnel.cpp`):

```cpp
double rs = (n1*ci - n2*ct) / (n1*ci + n2*ct);
double rp = (n2*ci - n1*ct) / (n2*ci + n1*ct);
double R  = 0.5 * (rs*rs + rp*rp);
```

It **already computes rs and rp separately and averages them**. So the polarised version returns
`{rs, rp}` (amplitude coefficients) and an unpolarised caller forms `0.5 * (rs*rs + rp*rp)` — the
same three operations in the same order, therefore the **same double**. Assert it with `==` in a
test that drives both paths over a sweep of angles and indices.

This is the single most reassuring fact about Wave 5: the existing physics is already factored the
right way.

## 5.3 Unpolarised light

A single Jones vector is *fully* polarised. Sunlight is not. Two honest options:

1. **Randomise the input polarisation per ray.** Each primary ray gets a uniformly random linear
   (or random elliptical) state; averaged over many rays this reproduces unpolarised statistics.
   Cheap, correct in the mean, and it costs one or two RNG draws per ray — **which shifts the RNG
   sequence and therefore moves every existing result.** Only acceptable if gated behind a flag
   that is off for solar scenes.
2. **Carry a flag**: `bool polarised` on the ray; when false, materials use the averaged
   (existing) Fresnel path and skip Jones algebra entirely. Solar scenes are bit-identical because
   they run exactly the old code.

**Take option 2.** Option 1 is more elegant and would move all five goldens for no benefit to the
solar use case. A source declares whether it emits polarised light; the sun does not, a laser does.

## 5.4 New materials

| Type | Parameters | Behaviour |
|---|---|---|
| `polariser` | `transmission_axis_deg`, `extinction_ratio` | Projects the Jones vector onto the axis. A real sheet polariser passes ~38% of unpolarised light and has an extinction ratio of 10³–10⁵; an ideal one passes 50%. |
| `waveplate` | `retardance_waves` (0.25 = quarter, 0.5 = half), `fast_axis_deg` | Adds relative phase between the fast and slow axes. |
| `polarising_beam_splitter` | `extinction_ratio` | Transmits p, reflects s. Uses `Split` with the two branches carrying the two components. |

## 5.5 Interference

- **Accumulate optical path length**: `opl_m += n_medium * distance` on every segment. The medium
  index is the problem — `Scene::intersect` has no medium tracking (gap G5 in the Wave 3 doc), so
  inside a dielectric body the index must come from the material the ray last entered. A per-ray
  `current_n` field solves it for non-overlapping bodies, which is the documented limit anyway.
- **A coherent accumulator mode**: sum `amplitude * exp(i * 2π * opl / λ)` per bin and take
  `|Σa|²`, instead of `Σ|a|²`. Complex addition is associative, so the per-thread merge still
  works unchanged.
- **Coherence length** on the source: rays whose path difference exceeds it add incoherently.
  Without this every source behaves like an ideal laser and fringes appear where they should not —
  a white-light source would show fringes across the whole screen.

## 5.6 The pitfall that decides whether this works at all

**Random Monte Carlo sampling summed coherently produces speckle, not fringes.**

Coherent summation of randomly-placed rays gives a random-phase sum, which is a speckle pattern —
noise that looks like structure. A coherent run needs **deterministic, stratified sampling** over
the source aperture (a regular grid, or a low-discrepancy sequence), not the random sampling the
solar path uses.

So: the laser source must offer both sampling modes, and the interface must not let a coherent
detector run against a randomly-sampled source without saying what is on screen. If this is got
wrong, the result is a picture that looks like a physical result and is numerical noise — the worst
possible failure for this application.

## 5.7 Gates

- **All five goldens bit-identical**, because solar scenes take the unpolarised path unchanged.
- Fresnel: polarised and unpolarised paths agree exactly over a sweep.
- **Crossed polarisers extinguish** — transmission ∝ cos²θ (Malus), zero at 90°.
- **A quarter-waveplate at 45° turns linear into circular** and back.
- **A Michelson with a known path difference produces the analytic fringe spacing.**
- Ray-size performance measured before and after on the corpus, and the number recorded.

## 5.8 From the user's hands-on testing (added mid-wave, after Stage 4)

**Quick features, before Stage 5** - none of them depends on the physics still to come:

1. Double-click a scene in the Scene Browser loads it.
2. A toolbar over the 3D view with Preview and Full Trace, so checking a design needs no tab switch.
   The Simulate tab stays as it is.
3. A laser is drawn as a body: a housing that ends at the source's origin, the beam leaving its front.
4. Laser QA scenes get receiver grids near 0.1 mm per bin; at 48x48 over 20 mm a 2 mm beam covered
   about five bins.

**Stage 7 - user feedback, after Stage 6**, because the coherent receiver changes what the flux map
shows and Stages 5-6 give each ray the path and phase an inspector ought to show:

5. **The flux map uses a fixed scale taken from the source** (the user's choice over locking the scale
   on a chosen trace, or letting the first trace set it). 100% is the total source power divided by
   the receiver area - the flux if all the light fell evenly on the screen. A weaker design therefore
   visibly dims, and a focused spot may read above 100%. A sensitivity slider rescales the display;
   nothing adapts to the trace. For a coherent receiver the same reference applies to intensity.
6. Rays carry an indicator of wavelength (colour) and polarisation (a marker), and clicking a ray
   reports its wavelength, power, polarisation state, optical path length and phase.

---

### 5.8.1 Stage 8 - live update and movement sweeps (added after Stage 7, at the user's request)

Measured first: release laser benches trace in 8-92 ms at full ray count (Michelson 19 / 23 / 47 / 72 ms
at 10k / 30k / 100k / 300k rays), so both are affordable without a new engine.
- **Live:** re-trace a small preview after every edit, on the GUI thread (the worker would lock the
  gizmo mid-drag), ray count adapting to hold ~30 fps.
- **Sweep:** one part, moved along or turned about a world axis over a range in N steps, each a full
  trace; flux maps kept; a player with scrub, play and a power / centre-flux vs offset plot. The scene
  document is never edited; the live pose is restored on close. Locked editing while open.
- Gates: `tests/test_sweep.cpp` (Michelson lambda/2 = one fringe cycle; Malus cos^2 exact), QA 17 and
  QA 18, screenshots of setup, running, playback and close. No tracer change, so no corpus run of its own.

# Wave 6 — diffraction, as a second engine

**Goal:** a slit produces a diffraction pattern rather than a sharp-edged bright rectangle.

Rays travel in straight lines and do not bend around an edge. No amount of ray tracing produces
this; it needs a wave-propagation step the ray tracer hands off to.

## 6.1 Architecture

1. The ray tracer runs as normal up to a **hand-off plane** the user places.
2. The complex field is **sampled on a grid** on that plane, from the rays arriving at it —
   amplitude, phase (from `opl_m`, Wave 5) and polarisation.
3. The field is **propagated** to the detector by an angular-spectrum or Fresnel transform.
4. Intensity at the detector is displayed.

This is a genuinely separate module (`include/scrt/wave/`, `src/wave/`), not an extension of the
tracer. It depends on Wave 5 for phase and on Wave 3's G3 (circular/annular apertures) for anything
worth demonstrating.

## 6.2 The FFT dependency

- **pocketfft** (BSD-3, header-only) or **kissfft** (BSD-3, tiny). Either is fine.
- **Not FFTW.** Its licence is GPL-or-paid-commercial, and this app ships as a binary.
- `vcpkg.json` currently lists: assimp, doctest, fmt, glm, imgui, implot,
  nativefiledialog-extended, nlohmann-json, spdlog. Per `CLAUDE.md`'s orchestration rules,
  `vcpkg.json` is owned by the wave integrator, not by a workstream.

## 6.3 Sampling and aliasing — the whole difficulty

The grid pitch Δx sets the maximum representable angle: θ_max ≈ λ / (2Δx). A field sampled too
coarsely aliases, and **an aliased result looks like a plausible diffraction pattern**. The panel
must show the sampling limit beside the result, and refuse (or loudly warn) when the geometry
exceeds it.

Fresnel number **F = a² / (λ z)** decides which propagator is valid: F ≫ 1 near field, F ≪ 1 far
field (Fraunhofer). Show it. Choosing the wrong regime silently is the other way this produces
confident nonsense.

## 6.4 Gates — analytic, not by eye

- **Single slit** of width *a*: intensity ∝ sinc²(π a sinθ / λ); first null at sinθ = λ/a.
- **Double slit**, separation *d*: fringe spacing λ/d, modulated by the single-slit envelope.
- **Circular aperture** of diameter *D*: Airy pattern, first null at sinθ = 1.22 λ/D.

Each of these has a closed form. Assert against it numerically, with a stated tolerance, rather
than looking at the picture and calling it right.

---

# Wave 7 — the optimiser

**Goal:** place the receiver (or any selected element) for maximum power or tightest focus.

## 7.1 The missing API, first

**`SceneEditor` cannot move the receiver at all.** `commit_transform` works only on surface ids,
and receiver faces are registered in the outliner with `surface_id = 0`. `ReceiverDoc` has a
transform and `Receiver::set_transform` exists, but no editor member touches either.

So Wave 7 starts with `SceneEditor::commit_receiver_transform(const math::mat4&)`, writing the live
receiver and `doc_.receiver.transform` together — a fourth case of the header's rule 2. Without it
the optimiser has nothing to call.

## 7.2 Variables and objectives — all offered separately

**Variables:**
- Receiver position alone (3 DOF). The smallest useful version.
- Receiver position and orientation (6 DOF).
- Any elements selected in the outliner: position and rotation each (6 DOF per element). This is
  what lets it aim mirrors, and the search space grows fast.

**Objectives** — report all three whatever is being optimised, so the trade is visible:
- **Maximum total power** — `FluxAccumulator::total_power_w()`.
- **Minimum spot size** — **`FluxAccumulator::encircled_diameter(fraction)` already exists**
  (`FluxAccumulator.hpp:37`). Use D90 (the circle containing 90% of the power). No new metric
  needed, which is worth knowing before writing one.
- **Maximum peak flux** — `peak_flux_wm2()`.

## 7.3 Search

Start with **coordinate descent in stages**, which is what `scripts/optimize_compact.py` already
does well: vary one parameter over a bracket, take the best, feed it into the next stage. Porting
its structure is cheaper and more predictable than inventing a search, and its staged
`BEST_R → BEST_HZ → …` chaining is a reasonable pattern.

Escalate to pattern search or Nelder–Mead only if measurement shows the staged version stalling.
Do not start with a gradient method: the objective is a Monte Carlo estimate, so it is noisy, and
finite-difference gradients on noise are meaningless.

**Noise matters and must be handled explicitly.** Two traces of the same scene with different seeds
differ. If the optimiser compares two candidates whose true difference is smaller than the Monte
Carlo noise, it will chase the noise. Either pin the seed across candidates (compares the same
random realisation — biased but consistent, and the right default) or raise the ray count until the
difference is significant. Say which, in the interface.

## 7.4 In-process, not process-per-scene

Every existing sweep (`scripts/sweep.py`, `scripts/optimize_compact.py`,
`scripts/fixed_box_analytic_optimization.py`) writes JSON files and shells out to `scrt_compare`.
An in-app optimiser instead mutates an in-memory scene and re-traces — a path that does not exist.

- Reuse `tracer::TraceControl` so it can be cancelled; the optimiser is a long-running loop and
  must not freeze the interface.
- Respect the existing "scene is immutable while a trace runs" gate — the optimiser both mutates
  and traces, so it owns that serialisation itself.
- Report progress per iteration: candidate, objective value, best so far.
- Keep or discard at the end. A discarded run must restore the document exactly.

## 7.5 Gates

- On a scene whose best receiver position is known analytically — a paraboloid of focal length *f*,
  whose optimum is at the focus — the optimiser finds it to within a stated tolerance.
- Cancelling mid-run leaves the scene exactly as it was.
- The five goldens unchanged (the optimiser must not alter the tracer).

---

# Wave 8 — the assistant, with tools

**Goal:** "make the reflectors 10 cm wider and re-trace" as a conversation with the scene.

## 8.1 Multiple providers

`send_claude_request` (`ClaudeClient.hpp`) is the single function boundary and `AppConfig` the
single persistence struct — but the transport hard-wires, all of it Anthropic-specific:

- host `api.anthropic.com`, path `/v1/messages`
- header **`x-api-key`**, not `Authorization: Bearer`
- header `anthropic-version: 2023-06-01`
- a **top-level `system` field**, not a system role message
- `output_config.effort`
- response shape: `content[]` filtered by `type == "text"`, `stop_reason` with the values
  `refusal` / `max_tokens`, `usage.input_tokens` / `output_tokens`
- errors read from `error.message`

Introduce a `Provider` interface with one implementation per vendor, each owning its endpoint,
auth header, request shape and response parsing. `AppConfig` grows a key per provider and a
selected provider. The transport stays WinHTTP; only the shapes differ.

**Keep the existing key handling honest:** the config is plain text in the user's roaming profile
and the panel says so. Do not let a multi-provider rewrite quietly drop that disclosure.

## 8.2 Tools over the live scene

The assistant gets a tool set and works in a loop:

| Tool | Maps to |
|---|---|
| `list_scene` | The document, serialised. |
| `add_element` | `PanelContext::add_element` (already queued and deferred). |
| `move_element` | `SceneEditor::commit_transform`. |
| `remove_element` | `SceneEditor::remove_element`. |
| `set_material_param` | `SceneEditor::commit_material_param`. |
| `set_sun` | `SceneEditor::commit_sun`. |
| `run_trace` | `PanelContext::run_trace`, then read the accumulator. |
| `read_result` | Total power, peak flux, D90. |

**This is why Wave 8 comes last.** Every one of those already exists because Waves 1, 2 and 7 built
them for other reasons; the assistant is mostly a dispatcher over an API that is already there.

**Three things it must have**, because the model is changing the user's work and spending their
money in a loop:

1. **A visible transcript** — every tool call and result shown as it happens, not after.
2. **A step limit**, with the count visible and a stop button that works mid-loop.
3. **An undo that restores the document.** `io::SceneDocument` is a copyable aggregate, so snapshot
   before the loop and restore on request is cheap and complete. Do this before wiring any
   mutating tool, not after.

## 8.3 Generate the schema from the code

The system prompt is a hand-written contract with `io::parse_document`, and it was wrong on its
first run: it told the model an absorber takes a `reflectance`, which strict mode rejects, so
**every generated scene would have failed to load**. `tests/test_assistant_schema.cpp` catches that
after the fact.

Generate the surface-type and material-type tables in the prompt from the same source the parser
uses — the `reject_unknown_keys` allow-lists and `validate_material_strict`'s map — so a new type
cannot be added to the engine without appearing in the prompt. The test then guards the *shape* of
the document rather than a hand-copied list.

## 8.4 Gates

- Tool round-trip on a scene with no API key: the dispatcher is testable headlessly by feeding it
  synthesised tool calls, with no network at all. **Do this** — it is the only part of the
  assistant that can be tested without spending money.
- Undo restores the document byte-identically after a multi-step loop.
- The step limit actually stops the loop.
- The schema generator's output parses under `strict = true`.

---

## Dependency summary

```
Wave 3 (libraries, G3 apertures)
   ├──> Wave 4 (geometry needs the new surfaces to draw)
   └──> Wave 6 (diffraction needs slits and circular apertures)
Wave 5 (polarisation, phase)
   └──> Wave 6 (propagation needs a complex field)
Wave 7 (optimiser) — needs only Wave 1's mutation API; can be pulled forward at any time
Wave 8 (assistant tools) — needs Waves 1, 2 and 7's APIs; genuinely last
```

Waves 7 and 8 have no physics dependency. If the bench matters less than automation on any given
week, take 7 early — nothing downstream cares.
