# Breast Chaos contact — implementation record

## Baseline and scope

Branch: `feature/chaos/breasts`. The branch was fast-forwarded to the accepted
`2922d9f` Jiggle baseline before starting contact work. Breast, hip and leg
secondary motion remain the native skeletal pipeline's responsibility.

Status: original C1 was installed; Step 1 repair and evidence are recorded below.
Compilation/publication alone never establish visible contact or visual acceptance.

## Research (2026-09-30)

* [Epic Chaos Flesh overview](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview)
  describes low-resolution tetrahedral simulation with a separate render surface.
* [Epic Flesh quickstart](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart)
  documents skeletal attachments, surface bindings and the Flesh deformer. World
  collision is rigid-to-deformable; this does not establish two-way rigid coupling
  or general soft-body self contact.
* Sheen, Larionov and Pai, [Volume Preserving Simulation of Soft Tissue with Skin](https://arxiv.org/abs/2109.01170),
  2021: near-incompressibility obtained solely through a Poisson ratio near 0.5
  can cause locking and numerical trouble. Their zonal volume constraints separate
  bulk volume conservation from local compression and skin response. This is a
  design reference, not a claim that stock Chaos implements their method.
* [Naturalis v1.3 physics parameters](https://everlastervr.github.io/docs/naturalis/v1_3/physics_parameters/)
  distinguishes joint dynamics from a surface soft-physics layer, with graded
  parameters and a separate pressure control. It is a useful architectural
  comparison; its parameter values are not UE material constants.
* Smith, de Goes and Kim, Stable Neo-Hookean Flesh Simulation (2018), is a further
  elasticity reference. The author-hosted PDF was located, but the browser could
  not fetch its full text. No implementation detail is attributed to an unread PDF.

## Local UE 5.8 source audit

`UFleshComponent` derives from `UDeformableGameplayComponent` and
`UDeformableTetrahedralComponent`. The latter owns a per-instance dynamic
collection and GPU buffer manager. `UDeformableSolverComponent` exposes private
solver creation, synchronous write/simulate/read, substeps and quasistatics.
`FPositionTargetFacade` permits weak attachment to a kinematic particle; attachment
should grade continuously toward the root instead of fixing a rectangular rear
quarter of a box.

Important integration trap: stock `DG_FleshDeformer` blends absolute embedded
positions with its own linear skinning. The stock graph's `ReadPosition` reads the
static vertex buffer; its kernel does not read Morph Target deltas. Attaching it
unchanged would therefore violate the existing Shape/Morph and Jiggle contract.
The contact graph must explicitly preserve Morph input and add a residual in the
correct coordinate space. Its normal update must use the resulting surface.

## Required composition

`Shape/Morph -> Animation/IK/rigid blend -> Breast Jiggle -> contact residual -> GPU surface`

The volumetric rest and attachment targets follow the final skeletal pose. Gravity
and moving-frame inertia are already handled by Jiggle and must not be applied
again by the contact layer. At zero contact residual the existing native result
must be reproduced, including Morphs. CPU work is limited to the small simulation
cage, contact queries and diagnostics; no full-body CPU skinning or replacement
ProceduralMesh is permitted.

Volume error must be measured using signed tetrahedral volumes against the current
rest volume. A positive absolute volume sum must not conceal inverted elements.
Contact penetration, minimum signed volume ratio and total volume error are
separate diagnostics. A fail-safe must report failure rather than silently claim
volume preservation. Unsupported collisions must be named explicitly.

Shape changes require per-instance rest recalibration; shared Skeleton/Profile
assets remain immutable. Teleport and pause clear invalid solver history. The
normal character-generation transaction must publish the cage, bindings, profile,
deformer and RuntimeConfiguration together, after independent reload.

## C1 implementation

* `VamBreastContactBuilder`: reuses the accepted Breast region evidence and retained
  source vertex identity. Builds each convex surface envelope with a chest-wall
  closure in the anatomical Anchor frame, then tetrahedralizes it with an interior
  point. This is a convex contact proxy, not a reconstruction of internal anatomy.
  Particle count is bounded; degenerate tetrahedra and missing surface bindings
  reject publication. No original bones or skin weights are changed.
* Particle attachments inherit native source weights. The root is kinematic on the
  chest; other particles have continuously graded weak attachments to ghost
  particles driven by the final native pose. Morph responses are saved on the
  simulation domain. A Shape revision rebuilds instance-owned rest collections.
* One private Chaos Flesh solver runs Gauss-Seidel quasistatics with no added
  gravity. At most one 1/120-second numerical solve runs per tick (12 iterations);
  there is no catch-up loop for this equilibrium contact layer. The residual is
  simulated position minus the analytically skinned, current-Shape cage. Without
  contact sources the solver and GPU override are released. While active, elastic
  relaxation can contribute to the residual; it is not an exact contact-only
  subtraction of an independent free equilibrium. The finite iteration budget
  does not guarantee identical transient convergence across frame rates.
* A per-side zonal volume projection runs inside the Chaos constraint iterations.
  For `C = sum(signed tet volumes) - rest volume`, each free particle receives
  `-inverseMass * gradient(C) * C / sum(inverseMass * |gradient(C)|^2)`.
  Collision projection follows; finite iteration counts leave a measurable error.
  Native corotated elasticity supplies local shape resistance. This is not the
  complete skin/FEM method of Sheen et al. and not a Stable Neo-Hookean implementation.
* The cloned GPU graph explicitly reads native Morph deltas, skins with the final
  bone matrices and adds the embedded contact residual. Normals are recomputed on
  the GPU. The native SkeletalMesh stays visible. No full-body CPU skinning path.
* The RuntimeConfiguration references `DA_BreastContact`, which references
  `DG_BreastContact`. `AVamCharacterActor` owns the contact component. Ordinary
  generation and Upgrade Runtime include these assets without another build button.

## Observation controls

VaM character debug panel -> Breast Jiggle · Runtime ->
**Breast Chaos Contact · 按压与体积**:

* Enabled: switches the contact layer independently of Jiggle.
* Show cage: displays the simulated volume boundary.
* Show press spheres: displays explicit/debug press sources.
* World collision: discovers nearby StaticMesh components with simple collision.
* Left/right press 20%: places an anatomical-frame sphere into the selected side.
* Release: removes the debug press sphere.
* Reset contact: recreates private solver state, including after an inversion fault.
* Diagnostics: signed cage volume, relative volume error, minimum tetrahedron ratio,
  inverted-element count and solver step count. These are not visual pass/fail labels.

Blueprints may supply `PressSpheres` in world space; their state belongs to the
character instance. Native world support currently covers sphere, box and convex
simple collision as provided by the engine Flesh collision component.

## Limits

* One-way rigid-object-to-soft-volume response. No breast/breast self collision,
  deformable/deformable contact, rigid reaction forces or automatic skeletal-hand
  collider import in C1. StaticMesh triangle-mesh complex collision is not supported.
* Convex proxies cannot represent arbitrary concave tissue geometry. Surface contact
  accuracy depends on cage resolution, bindings and the continuous region mask.
  Cage volume conservation is not proof of exact rendered-surface volume conservation.
* Large Shape changes can invalidate the cage. Degenerate rest geometry rejects
  initialization; non-finite output or inverted simulation tetrahedra disables
  contact and reports an error. Increase in pressure is not a guarantee of recovery.
* LOD0 binding only. No general multi-LOD contact contract is claimed.
* GPU graph construction and asset reload are distinct from a rendered runtime
  observation. Material parameters, pressure range, frame cost and visual behavior
  still need in-world assessment. No new automated test suite was added or run.

## Delivered assets and build evidence

Latest immutable output:

* `/Game/VamRuntime/R_5152610a7094285909b90173/BP_VamCharacter`
* `/Game/VamRuntime/R_5152610a7094285909b90173/RC_Runtime`
* `/Game/VamRuntime/R_5152610a7094285909b90173/DA_BreastContact`
* `/Game/VamRuntime/R_5152610a7094285909b90173/DG_BreastContact`

Drag the new BP into an ordinary level, enter PIE, select the runtime character
in the existing VaM debug panel and expand the contact controls described above.
Use Show press spheres, press left/right and Release. Observe volume diagnostics
alongside the surface; a small cage volume error alone does not establish surface
contact accuracy. Existing BPs need a newly generated Runtime bundle.

Evidence under the plugin's `Saved/` directory:

* `LegJiggleT1/PrimaryBreastChaosC1Final/runtime-report.json`: committed,
  independent reload and separate publication verifier, all phases exit 0.
* `BreastChaosC1/editor-build.log`, `game-build.log`: final builds succeeded.
  Game required retry after an MSVC internal compiler error. Editor linking required
  a retry without UBA after transient Windows SDK file-access errors.
* `BreastChaosC1/cook-final.log`: targeted Windows cook succeeded, 713 packages,
  0 errors, 0 warnings. Four `DG_BreastContact` PCD3D_SM5 compute kernels compiled
  successfully. The first cook attempt used the wrong Content alias and included
  Landmass editor references; its failure is retained in `cook.log`.
* `BreastChaosC1/installed-binaries.json`: installed DLL SHA256 hashes.
* Previous installed binaries: `BreastChaosC1/Backup-20260930-002844`.

Source is on `feature/chaos/breasts`, based on `2922d9f`; implementation is an
uncommitted working-tree change. No commit/push or source-asset migration performed.

## Press initialization / debug scrolling fix (2026-09-30)

The contact runtime incorrectly subtracted the authored Morph baseline from
USkeletalMeshComponent::GetMorphTarget. ApplyMorphWeights already converts absolute
appearance values into mesh-relative weights, so imported appearance was subtracted
twice. Contact now applies the mesh-relative weight directly. Invalid cages still
fail closed, with the first invalid tetrahedron and signed volume in diagnostics;
reset explicitly retries initialization.

The debug tab now scrolls as a whole. Breast, structural glute and contact diagnostic
text have independent height-limited scroll regions (180 Slate units). Existing C1
profiles/BPs remain compatible; no asset regeneration is necessary.
Editor build succeeded and plugin binaries installed (press-fix-build.log,
press-fix-installed.json). No scene simulation or visual validation was performed
for this fix; successful compilation does not establish the press response quality.

## Collision identity / material usage repair (2026-09-30)

User PIE logs identified an abstract UObject allocation for press collision keys,
and missing material usage flags immediately after contact reset. Collision keys
now use a concrete transient UVamBreastContactSource. Reset restores the prior
component deformer override instead of unconditionally setting a null override.
Debug pressing uses the actual cage front point rather than combining front depth
with COM transverse coordinates. Diagnostics expose press count, selected side and
maximum contact-minus-free particle displacement in cm.

Generated body materials now own MeshDeformer, SkeletalMesh and MorphTarget shader
usage permutations, including reference body slots. Source materials and old runtime
bundles are not modified. The new bundle is required for persistent material repair.
Editor compilation succeeded (contact-object-fix-build.log). Scene response and
rendered output have not been validated by this change's build result.

Repair bundle published successfully:
`/Game/VamRuntime/R_f89178069bfcd90f35a9af2b/BP_VamCharacter`.
Build, separate reload, and publication verification all exited 0; evidence:
`Saved/LegJiggleT1/PrimaryContactObjectFix/runtime-report.json`.
DLL installation is pending user closing the running UE editor.


## Step 1 — press delivery and performance repair (2026-10-07)

Scope: repair the existing C1 press-to-surface path, idle/disable lifecycle and
cost. This does not add breast self-contact, soft-soft contact, rigid reaction,
new anatomical tetrahedralization, or a full contact surface model.

Changes:

- The debug panel resolves the selected editor actor to its PIE counterpart.
- Press spheres target an actually movable vertex in the current animated cage,
  excluding both authored kinematic particles and particles Chaos made immovable.
  Penetration ramps at 0.4 depth-fraction/second and is capped at 0.8 sphere radius.
- One Gauss-Seidel quasistatic solver replaces two constantly running solves.
  Analytic cage skinning supplies the animated baseline. Twelve iterations run at
  most once per tick; there is no backlog of high-cost contact substeps.
- A shared proximity query gathers simple static-mesh contact sources. With no
  source, no solver/producer exists and the native deformer override is restored.
  Releasing the last debug sphere restores native output immediately; smooth
  viscoelastic unloading is not implemented in this stage.
- Per-side volume-rule gradients are reused. Diagnostics expose total/solve/
  publication CPU time, actual movable particles, penetration and binding transfer.
- CPU bound-surface displacement is explicitly a diagnostic prediction. Optional
  automation uses real RHI scene capture and GPU vertex readback separately.
- Reset/release clears stale volume/contact diagnostics and preserves material slots.
  Generated normal kernels also use the correct >= thread bound check.

Engineering checks:

`Vam.Breast.ContactRuntime` creates an isolated ordinary game world, loads a Runtime
configuration and runs 90 ticks per phase. It asserts left/right press transfer,
finite signed volume within 5%, zero inverted elements, preserved material slots,
zero idle/disabled solvers, native deformer restoration, reset, translation and
explicit 90-degree teleport. Optional `-VamContactGPU` uses a scene capture and
`RequestReadbackRenderGeometry`; it isolates breast jiggle during the GPU A/B
comparison. This is not a visual/naturalness test.

A raw instantaneous Actor rotation without notifying Motion produced an inversion
in the first regression experiment. The supported relocation path is
`UVamMotionComponent::TeleportTo`, which resets the native Jiggle history as well
as contact. The failure is preserved in `step1-assertions.log`; the explicit
teleport regression succeeds in `step1-teleport.log`.

Performance evidence is an isolated character, not a guarantee for every level:
old NullRHI world tick was about 22–24 ms even idle. The repaired world tick is
about 0.5 ms idle/released and 6–7 ms while pressing. A real RHI capture run measured
roughly 3–5 ms idle/released and 10 ms pressing, including capture and render-thread
synchronization. These are not GPU-only timings and not a user-level FPS promise.

Remaining limits: coarse convex/star cage, vertex-based one-way simple rigid
contact, possible non-contact elastic relaxation while solver is active, finite
iteration/frame-rate-dependent transient convergence, immediate release, and
unsupported deep penetration/extreme poses may fail closed on inversion. Shape
changes can invalidate this coarse cage. These require later-stage work rather
than stronger arbitrary press offsets.


### Step 1 output / reproduction

Generated immutable bundle (build, reload and separate verify all exited 0):

- `/Game/VamRuntime/R_076723ea853a483f409d0cfc/BP_VamCharacter`
- `/Game/VamRuntime/R_076723ea853a483f409d0cfc/RC_Runtime`
- `/Game/VamRuntime/R_076723ea853a483f409d0cfc/DA_BreastContact`
- `/Game/VamRuntime/R_076723ea853a483f409d0cfc/DG_BreastContact`

In an Empty Level, drag this BP into the level and start PIE. Select its runtime
instance, open the VaM character debug panel and use the selected-character refresh.
Expand Breast Jiggle, then Breast Chaos Contact. Enable contact and use left/right
20% press. Show press spheres displays the contact source. Release should restore
native output; disabling contact should show zero solvers. The show checkboxes only
control drawing. World collision enables nearby other-actor static-mesh simple
collision, not general soft-body or skeletal hand contact. Diagnostic boxes and the
whole panel scroll separately. Old placed BPs are not silently replaced.

Editor and Game builds: `Saved/BreastChaosC1/step1-delivery-build.log` and
`step1-game-build.log`. Installed DLL hashes and recoverable backup path:
`step1-installed.json`. Immutable generation evidence:
`Saved/LegJiggleT1/PrimaryContactStep1/runtime-report.json`.

Source HEAD remains `2922d9fe16616cd8f182cdc49455aa8429e7be19`, with the C1 and Step 1
implementation in the working tree on `feature/chaos/breasts`; no commit or push.


Final new-bundle real-RHI test: `step1-final-gpu.log`, Automation Success. With
Breast Jiggle disabled for the pressure A/B measurement, GPU readback returned
25,066 vertices and maximum pressure displacement **1.0791 cm**. Left/right steady
press cage-volume errors were -0.02% / -0.09%, with zero inverted tets. These are
proxy cage volumes, not anatomical tissue volumes or a guarantee of surface volume.
Reset, translation, explicit teleport rotation, deformer removal and material-slot
assertions passed. Release/disabled capture ticks measured 3.273/3.244 ms; press
10.255/10.071 ms. Initial idle had a 17.785 ms world/capture outlier while contact
itself reported 0.003 ms and zero solvers; do not hide that startup measurement or
attribute all scene cost to this component. `step1-evidence.json` retains all phases.


Targeted Windows cook succeeded: `Saved/BreastChaosC1/step1-cook.log` reports
708 cooked packages, 7 platform-skipped, 715 total; 0 errors and 0 warnings.
All four DG_BreastContact compute kernels compiled successfully for PCD3D_SM5.
This is a targeted cook, not a packaged-game launch test.


Final new-bundle native/Jiggle-enabled test also passed (`step1-final-native.log`):
NullRHI world tick idle 0.718 ms, disabled 0.495 ms, left/right press 7.047/6.803 ms,
release 0.485 ms. The native/Jiggle test and isolated GPU-pressure test are separate
checks; neither declares visual acceptance. Step 1 code, installed binaries,
committed new assets, runtime assertions, GPU transfer evidence and targeted cook
are complete. Steps 2/3 remain unimplemented.

## Step 2 delivery (2026-10-07)

Step 2 is now implemented; see `BREAST_CHAOS_CONTACT_C2.md` for the final
surface/volume constraint ordering, source-derived nipple material refinement,
latest immutable BP, build/test/cook evidence, performance limits and actual UE
comparison images/video. Step 3 remains pending. The earlier Step 1 completion
statement above describes the historical state at that delivery.
