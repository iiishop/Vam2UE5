# Breast Chaos Contact C2 — volume and pressure shape

Branch: feature/chaos/breasts. Base HEAD 2922d9fe16616cd8f182cdc49455aa8429e7be19.
Implementation remains in the working tree, no commit/push.

## Scope and references

Step 2 extends the single Chaos Flesh quasistatic contact solver. It does not add
self-contact, soft-soft coupling, rigid-body reaction, or a new Jiggle solver.

- Sheen, Larionov, Pai (2021): https://elrnv.com/projects/volume-preserving-simulation-of-soft-tissue-with-skin/
  separates zonal volume, cell compression and skin response. C2 borrows that
  separation; it is not their full nonlinear FEM/epidermis implementation.
- Epic 5.8 overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-flesh-overview
- Epic quickstart: https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart
  documents low-resolution simulation/render bindings and vertex-based one-way
  rigid collision. Surface sampling density matters for small contact patches.

## Builder and schema

C2 is explicitly written as schema 2 in the Builder. The class default remains
schema 1 so old assets whose default version was omitted by serialization are not
silently reinterpreted. New immutable Runtime output is required for C2.

The evidence-derived convex cage remains the source envelope. Conforming longest
surface-edge bisection splits every adjacent tetrahedron and boundary triangle;
there are no hanging nodes or altered neutral surface positions. Target spacing
is 0.35 calibrated effective radius, capped at 96 additional particles per side.
This improves contact coverage, not anatomical reconstruction of internal tissue.
Root-root edges remain fixed and do not consume the movable-surface budget.

Midpoint skin weights and Morph deltas are interpolated from endpoints. Interior
center skin and Morph response now use the same mean of boundary samples used by
its geometric rest position, replacing the inconsistent nearest-source sample.
Every render binding is regenerated in the refined cage. Build checks require
unchanged signed total volume (relative tolerance 1e-8) and zero-offset binding
reconstruction within 0.001 cm. Original Skeleton, bone indices and skin weights
are not modified.

## Constraint composition

Chaos GS elasticity and weak animated attachments remain. UE PBDEvolution can
run separate constraint ranges concurrently by default. Native GS and our rules
modify the same particles, so C2 chains native GS plus compression/volume in the
same rule slot. It fails closed for an unsupported native rule layout instead of
globally changing parallelism for other characters or cloth.

Each solver iteration executes:

1. Native GS elasticity and attachments.
2. Cell compression: if J=V/Vrest falls below 0.35, mass-weighted volume-gradient
   projection restores towards the threshold with relaxation 0.65.
3. Per-side zonal signed-volume correction. Backtracking rejects corrections
   that would reduce a cell below J=0.08.
4. Native Chaos rigid collision projection.
5. Contact-aware surface strain: four alternating edge sweeps target length
   ratios [0.65,1.30]. Corrections near active Chaos implicit rigid geometry have
   inward directions removed; moved endpoints are reprojected against that same
   geometry. This prevents skin constraints and collision from blindly undoing
   one another. Each endpoint correction is capped at 0.25 edge rest length.

These are finite-iteration targets, not guaranteed final bounds. Actual cell
compression, volume, stretch and probe penetration are reported separately. The
post-collision surface rule is INSIDE every Chaos iteration, not an after-solve
render mesh inflation. Contact remains one-way against rigid simple geometry.

No added gravity or moving-frame inertia. Final native pose drives the attachment
reference, and the GPU adds the contact-minus-animated-cage residual. With no
contact source, native skinning is restored and the contact solver is released.
A 0.0002 kg quasistatic numerical mass floor prevents tiny positive-volume cells
from being silently turned kinematic by Chaos's nearly-zero mass test. This is
solver conditioning, not a claim about real anatomical mass.

Debug indenter radius is 0.55 effective radius (C1 was 0.30); this creates a broader
contact patch. Nominal penetration remains depth-relative and capped at 0.8 sphere
radius. Greater default visible displacement must not be attributed solely to the
material model: the diagnostic indenter is larger too. Release ramps the indenter
out before restoring native output; this is controlled unloading, not viscoelastic
or dynamic soft-tissue recoil.

## Nipple material refinement (user feedback)

The uniform-material capture exposed flattening of the protruding nipple detail.
New builds consume the same family JSON as the rest of the native pipeline. The
left/right front-reference bone mapping identifies the source nipple bones;
source skin support and family-selected nipple Morph vertex-delta support are
normalized per side with a smoothstep and interpolated onto refined cage vertices.
The inspected source has nipple reference bones but zero nipple bone skin weights;
its real nipple Morph supplies the material evidence. Closure and interior center have zero nipple support.
Missing family/skin/Morph evidence is reported as unsupported instead of guessing a
world-space sphere or naming a particular character.

A bounded 32-node local distance graph per side adds cross-links inside this
region. Edge strain limits smoothly narrow from the general skin limits toward
1 +/- 0.03 according to endpoint support. These are finite-iteration engineering
targets, not a measured biological modulus. The same contact-aware projection
prevents local retention from pushing the cage through the indenter. The region
is not kinematic: translations and rotations do not change the constraints.
Shape updates continue to derive all target distances from current shaped rest.

`NippleShapePreservation` in the profile and per-instance
`NippleShapePreservationScale` (0..1) allow an identical-pressure material A/B test.
Zero restores the uniform strain limits; it does not disable the full contact
layer. Diagnostics report local pair-distance RMS strain and sampled pair count.
The added test requires local strain to decrease by at least 10% while ordinary
contact displacement, volume, inversion and surface-stretch checks still pass.

Research supports material differentiation, but does not establish a universal
nipple-to-breast stiffness ratio. Do not describe the nipple as an always-rigid
anatomical object. Relevant primary studies:

- In vivo measurement of breast tissues stiffness using a light aspiration device
  (2022), https://pubmed.ncbi.nlm.nih.gov/36099706/ : a seven-volunteer pilot that
  estimated distinct skin and internal tissue stiffness; not nipple calibration.
- Smooth muscle morphology in the nipple-areola complex (2011),
  https://s3.amazonaws.com/host-article-assets/jms/587cb49d7f8c9d0d058b4796/fulltext.pdf
  : anatomical histology, not a quantitative material law for this implementation.

## Evidence protocol

Real UE scene capture, fixed camera/light/pose; 3/4 and side views. Native material
and compute shader compilation is explicitly completed before export, avoiding
misleading default-material captures. Jiggle is disabled before the normal capture and throughout the pressure
comparison so secondary motion cannot explain the visible displacement. No AI
image generation or manually moved vertices are used in the evidence images.
A 6-second / 30 fps image sequence records neutral, gradual press, hold and release.
The comparison viewer links the raw normal/pressed/released PNGs and encoded video.

Engineering checks cover GPU readback, signed volume, inverted elements, min J,
surface strain, material-slot identity, idle/disable cleanup, explicit teleport,
neutral binding and independent asset reload. This is not visual acceptance.

## Limits

The envelope is still convex with radial/star tetrahedra, not a high-quality
anatomical layered volume mesh. Bisection adds samples but cannot remove every
sliver. Surface edge guards are not a complete skin elasticity/area/bending model.
Cage volume is a proxy and does not establish exact visible-surface or anatomical
volume. Deep penetration, extreme pose/Shape and incompatible root/contact
constraints may exceed tolerances or fail closed on inversion. Finite quasistatic
iterations do not guarantee frame-rate-independent transient convergence. Dense
sampling costs more CPU while contact is active; idle still avoids simulation.

## Delivered 2026-10-07 (includes nipple feedback)

Latest immutable bundle:
`/Game/VamRuntime/R_2c2d093234cf7ef1a87f8240/BP_VamCharacter`.
Same root contains `RC_Runtime`, `DA_BreastContact`, `DG_BreastContact`.
Cage: 457 particles / 902 tetrahedra; 384 movable particles. Each nipple local
shape diagnostic includes 153 pairs (18 supported vertices per side).

Earlier C2 bundles R_99fca27..., R_27a6b..., R_b1e5b... are exploratory outputs;
the latter predates the user's nipple material feedback. Do not use them as the
latest delivery. Existing scene instances are not silently replaced. Original
source assets and their material assignments remain intact.

### Verification

- Editor: `Saved/BreastChaosC2/editor-nipple-morph-build.log`, Succeeded.
- Game: `Saved/BreastChaosC2/game-nipple-build.log`, Succeeded.
- Immutable build / independent reload / verify: all exit 0, committed receipt in
  `Saved/LegJiggleT1/PrimaryContactStep2NippleMorph/runtime-report.json`.
- GPU runtime automation: `runtime-nipple.log`, Success. Real GPU readback returned
  25,066 vertices, maximum zero-pressure-to-pressed displacement 2.7405 cm.
  Left/right steady press cage-volume errors -0.03% / -0.03%; minimum J
  0.4820 / 0.4813; maximum surface edge stretch 1.264 / 1.262; zero inversions.
  Local nipple RMS pair strain 0.145353 uniform vs 0.095078 refined, 34.59% lower.
- Native/Jiggle-enabled runtime automation: `native-nipple.log`, Success.
  Nipple RMS 0.136562 -> 0.099057. Checks also cover release, material identity,
  reset, translation, explicit rotation teleport and native-deformer restoration.
- Targeted Windows cook: `cook-nipple.log`, 0 errors / 0 warnings. All four
  DG_BreastContact kernels compiled for PCD3D_SM5. Not a packaged executable run.
- Installed five DLL/PDB/module files after confirming interactive UE closed;
  recoverable backup and source/destination SHA256 in `installed.json`.
- Consolidated evidence: `Saved/BreastChaosC2/evidence.json`.

This test set is one source character with bilateral pressure and multiple world
transforms. It does not validate every body Shape or arbitrary severe contact.
Original neutral binding and volume checks execute during asset generation.

### Performance limits

Native NullRHI isolated-world mean ticks while pressing: left 13.674 ms, right
13.219 ms. Last component solve samples were about 12.0 / 12.5 ms. Disabled world
mean tick was 0.495 ms; contact itself about 0.001 ms with zero solvers. Release
phase mean 4.736 ms includes controlled unloading before returning to idle.
These are local isolated measurements, not a game-frame-rate guarantee. Active
contact remains a substantial CPU cost; this step prioritizes contact/shape
correctness and must not be advertised as a completed performance optimization.

### View / reproduce

`Saved/BreastChaosC2/visual-nipple/compare.html` shows raw normal vs pressed side
and 3/4 captures, same-depth uniform-vs-local-material images, and the actual
6-second 768x768 / 30 fps press/hold/release MP4. The 180 PNG frames remain beside
it. Fixed light, camera, pose and Jiggle disabled throughout the image comparison.
The pressing sphere is intentionally not drawn so the contour is unobstructed.
These images still show coarse local faceting and do not constitute visual approval.

In a normal level place the latest BP above, Play, select its live instance in
VaM Character Debug, expand Breast Chaos Contact, then press left/right 20%,
release or reset. Show Cage and Show Press Spheres are optional overlays. Old
placed BP instances retain their old RuntimeConfiguration; select/load the latest
RC or use the newly generated BP. The per-instance Nipple Shape Preservation Scale
is in the BreastContact component's VaM / Breast Contact details (1 default,
0 uniform-material comparison). Profile defaults remain immutable shared data.

Step 3 remains pending: self/soft-soft collision, arbitrary other soft bodies,
and two-way rigid reaction are not implemented by these changes.

## Body compression follow-up

The next iteration and its validation are tracked in
[BREAST_CHAOS_CONTACT_C2_BODY.md](BREAST_CHAOS_CONTACT_C2_BODY.md).
It replaces the nipple-only contact targeting and one-center cage with explicit
body contact and a conforming interior layer. See that document for current
status; the nipple-only evidence above belongs to the previous installed build.
