# G1 — Glute Dual-Attachment Secondary Dynamics

Gravity behavior for newly generated schema-2 assets is superseded by
[G1.1 Reference-Gravity Equilibrium](GLUTE_JIGGLE_G11.md).
See the [G1.1 verified report](Evidence/GluteJiggleG11/REPORT_ZH.md) for current assets and results.

Status: implemented, compiled, installed and engineering-verified on 2026-09-29.
Full results and Chinese operating instructions: `Evidence/GluteJiggleG1/implementation-report.md`.
Baseline HEAD is `119c23b8aa7f9ca96673f1c2ca611a15304b320a`; G1 changes are uncommitted.

## User-selected character defaults (2026-09-29)

New runtime components default to Support **0.45**, Damping **0.65**, Mobility
**2.0**, Internal Coupling **1.0**, Mass Scale **1.0**. These are the enhanced
settings accepted by the user. The low-level `FVamGluteTuning` identity remains
all ones for calibration and isolated mechanics; the character component always
passes its configured values through `GetGluteTuning`. Historical benchmark tables
below and in evidence used all-one multipliers, not this later user-selected preset.
Existing serialized instance/Blueprint overrides retain their explicit values.

## Surface participation / damping refinement (2026-09-29)

Current replacement outputs and Breast comparison are documented in
`Evidence/GluteJiggleG1Surface/implementation-report.md`. G0 algorithm is now
`glute-structure-g05-surface-v3`; G1 is `glute-dual-attachment-g1-v2`.
Source evidence masks and geometric calibration are unchanged from surface-v2.
The family policy maps evidence confidence into a separate skin participation:
`transfer = 0.7 * smoothstep(clamp(region / 0.45)) * posteriorRootFade` for
VamFemale88. Both policy values are in family JSON and serialized into the
structural profile/skin identity. At least 30% of eligible original donor weight
remains; unrelated bones and the eight-influence compression policy are unchanged.
This changes derived skin weights and the rebuilt corrective calibration, not the
source assets. A trial 0.8 donor ceiling failed source-corrective retention and
was rejected without lowering the existing fidelity bound.

The editor `SurfaceTransferAudit` measures actual native skin response to a
coherent one-centimeter helper translation, independently of solver/debug points.
Panel walk/jump/turn trajectories now have sampled native surface regression data.
No free whole-glute angular mode was introduced. Breast retains its own modes.

## Spatial calibration correction (2026-09-29)

The initial G1 outputs below were found to inherit an invalid G0 spatial
calibration: vertex-count statistics overrepresented dense perineal topology and
included retained vertices with no triangles. A passing dynamics test did not
prove anatomical placement. See `Evidence/GluteJiggleG1Fix/implementation-report.md`
for replacement outputs and verification. Do not use the original G1 assets to
evaluate the corrected calibration.

`glute-structure-g05-surface-v2` uses triangle-area quadrature for source evidence,
regional moments, semantic centroids, donor attachment statistics, and Shape
remeasurement. Vertices with zero surface area receive zero region weight. The
existing G0 kernels, append hierarchy and donor compression remain; regenerating
G0 recalibrates its helpers, transferred weights, volumes and downstream G1 data.
No runtime vertex skinning or solver strength multiplier was added by this fix.

`VamGluteStructureBuilder::SpatialAudit` compares each helper with the area-weighted
centroid of its **actual generated skin influences**. Publication of the new
algorithm rejects unused region evidence, absent influence support, or normalized
helper/support separation >= 0.25 of anatomical dimensions. This is a spatial
consistency bound, not a visual-quality test. Old committed assets remain intact;
their runtime diagnostics request Upgrade Runtime. Updating DLLs alone cannot
repair the positions or weights serialized in an old asset.

## Contract and integration

`Primary animation / IK / rigid blend -> primary pelvis/femur snapshot -> G0.5
structural helpers -> G0.6.2 native corrective Morph weights -> G1 helper translation
residual -> Breast finalization -> native skeletal skinning`.

G1 uses the existing twelve Glute bones. Each side has a kinematic Anchor and five
dynamic translations: Core, Upper, Lower, Medial, Lateral. Existing G0.5 helper
orientation/scale are preserved. No new skeleton indices, free whole-region angular
mode, Morph targets, procedural mesh, CPU runtime skinning,
contact, or G2 are introduced by the solver. The surface-v3 builder refines G0
weight participation as described above. Both Breast and Glute read primary bones; their
output bone sets are disjoint. G1 is disabled when the structural layer is disabled.

`UVamGluteJiggleProfile` is immutable calibration. All particles, previous frames,
accumulators, and diagnostics are component-instance state. Runtime initialization
loads the profile through `UVamRuntimeConfiguration::GluteJiggle`. Older bundles
without this optional reference retain their previous behavior.

## Minimal shared mathematics

`VamSecondaryMath::Solve` is the existing Breast 15-variable pivoted elimination,
extracted without arithmetic changes. Breast's modal decomposition, calibration,
moving-frame transport, angular mode and limits are otherwise unchanged. Existing
smooth actor debug trajectories are also reused as input, never as node offsets.
Glute has its own attachment/calibration policy and a five-particle solver.

## World-particle model and loaded dual supports

For a node, `X,V` are world position/velocity; `r(t)` is the current G0.5 rest in the
anatomical pelvis frame. Let `p,t` be current pelvis/femur attachment points and
`wP,wT` their G0.5 weights, summing to one. In current pelvis axes:

```
eP = (x-p) - (r-p)
eT = (x-t) - (r-t)
Felastic = -K * (wP*eP + wT*eT)
Fdamper  = -CP*(v-vP(x)) - CT*(v-vT(x))
vP(x) = originVelocityP + omegaP × (X-originP)
vT(x) = originVelocityT + omegaT × (X-originT)
```

This is a **prestrained tangent model around a prescribed loaded equilibrium**.
The elastic rest vectors are rebalanced by G0.5 as posture changes, so both spring
errors reduce to `x-r`. This is intentional: it does not simulate two independent
unloaded anatomical muscles. The two attachment dampers remain distinct and use
the actual different primary rigid-body velocity fields. A moving femur therefore
pulls the network even while the pelvis is fixed; its changing G0.5 geometry also
moves the spring equilibrium. No gait state enters the solver.

Uniform gravity is included as `m*g` plus structural preload `-m*g`. G0.5 is treated
as the loaded equilibrium for **every fixed pose**, satisfying the requested ON
settled == OFF contract. G1 consequently does not add orientation-dependent static
sag. Accelerating the pelvis, including jump/landing trajectories, still produces
relative inertia. The preload is a modeling convention, not measured anatomy.

World integration automatically contains the local-reference acceleration terms
`-a - alpha×r - omega×(omega×r) - 2*omega×vRelative` when transformed into pelvis
coordinates. They are not added a second time. Reference for this coordinate
identity: [MIT rotating axes lecture](https://ocw.mit.edu/courses/16-07-dynamics-fall-2009/resources/mit16_07f09_lec08/).

## Calibration and controls

Mass is G0 effective volume times internal density, distributed using its measured
regional mass fractions. COM, dimensions, attachment points and inertia candidate
are copied from existing character geometry. Volume remains a dynamics proxy,
not an anatomical measurement. Inertia is retained for future calibration; no
angular degree of freedom consumes it in G1.

`Kauto = G0.5 regional anisotropic support * DynamicModulusFraction` (default .08).
Current regional passive tension and attachment redistribution therefore enter K
without a whole-glute tension multiplier. No common fixed natural frequency is
assigned to all characters. Natural frequencies follow mass and support.

For G1 v2, baseline total damping per axis is
`C=2*zeta*sqrt(mAuto*Kauto)`, split as `CP=C*wP` and `CT=C*wT`.
Baseline zeta .32 therefore describes the combined attachment damper at automatic
mass/support, independently of the attachment split (coupled network modes may
have different ratios). Tuning still scales physical m, K, C independently.
The additive serialized flag `bNormalizedAttachmentDamping` defaults false for
legacy assets and is explicitly set true by the new builder. Legacy v1 assets
retain `CP=2*zeta*sqrt(mAuto*Kauto*wP)`, `CT=2*zeta*sqrt(mAuto*Kauto*wT)`;
their effective attachment damping was inflated by `sqrt(wP)+sqrt(wT)`.

| Control | Changes |
|---|---|
| Support (.1–10) | Attachment K only |
| Damping (.1–4) | Damper coefficients only |
| Mobility (.25–3) | Positive/negative travel only |
| Internal Coupling (0–4) | Sparse edge K only |
| Advanced Mass Scale (.1–10) | Effective particle mass; candidate inertia scales conceptually, unused by translation solver |

Because these controls are coefficient-orthogonal, changing Support or Mass Scale
also changes the *resulting* damping ratio; it does not silently recalculate C.
Pose tension changes the automatic support/damper baseline, never mass or Morph
weights. Travel uses dimensions, posterior depth, original attachments and G0.5
semantic tether, not current tension. This avoids using tension as hidden Mobility.

Eight edges connect Core to each other region and Upper/Lower to Medial/Lateral.
Edge stiffness uses geometric separation, attachment difference and geometric
mean regional support; peripheral edges are weaker. Forces are equal/opposite.

## Integration, limits, and lifecycle

120 Hz backward-Euler spring/damper network, solved as fifteen coupled linear
unknowns per side. Nonlinear stiffness is lagged per substep. The fixed-step force
matrix is positive definite for valid positive mass/support/damping. This favors
bounded stiff networks over an explicit high-stiffness render-delta integrator.
Backward Euler introduces numerical damping; this is an engineering compromise.
Nonlinear travel is evaluated using an end-time position predictor, not the
difference between an old-time particle and a new-time rest. A 300 cm/s constant
world-velocity boost changes the tested trajectory by only about 2e-12 cm.

The stiffness multiplier is `1 + 2*q² + 40*transition²`, where q is displacement
divided by the signed travel and transition begins at 65% of travel. Positive and
negative AP/SI travel differ. Emergency hard travel bounds remove outward relative
velocity, report a correction count, and are not contact collision.

Pelvis/femur motion uses endpoint estimates and Hermite positional interpolation;
orientation is slerped. Per-frame structural rest is interpolated across substeps.
Ordinary rest changes never reinterpret stored world position/velocity. Solver
render residual is relative to its last fixed sample, avoiding raw world-position
lag when rendering between substeps.

Explicit TeleportRevision, excessive pelvis translation/rotation or hip relative
rotation reset particles/history. A hitch exceeding the substep budget resets
instead of compressing elapsed motion. Pause freezes particles; resume primes
history. Small Shape previews/commits rebase position with the existing local
residual and preserve world velocity; large volume changes reset. Shape editing
does not become source motion. Skeleton assets and helper identity stay fixed.
The sleep flag is diagnostic; it does not skip integration yet.

## Generation and observation

The normal Generate UE5 Character Asset pipeline adds G1 calibration after the
existing G0/G0.6 build, saves `DA_GluteJiggle`, connects RC/BP, validates it in a
separate reload process and verifies publication. Upgrade Runtime uses that same
transaction and creates a new output; existing committed assets are retained.

In any ordinary Empty Level, drag in the **new** BP and use Play/Simulate. Open
VaM character debug, select/refresh that runtime character, then expand
**Glute Jiggle - G1**. Existing G0 structural pose controls still set fixed flexion,
extension or abduction. G1 controls work on the current pose:

- Walk Cycle / Alternating Thigh Swing: smoothly starts actual opposing thigh
  rotation input. Reset stops this cycle.
- Smooth Forward Accelerate / Smooth Stop: smooth actor linear motion.
- Lateral Accelerate: actor lateral motion.
- Smooth Turn / Continuous Turn / Smooth Turn Stop: smooth actor yaw motion.
- Jump: continuous takeoff, flight and landing actor trajectory.
- Hard Stop: abrupt velocity stress input.
- Reset: stop debug motion, clear pose debug offsets and reset dynamics.

Show Dynamic Nodes, Pelvis Attachment, Thigh Attachment, Velocity, Rest vs Dynamic
draw their named states. Diagnostics report per-node mass/rest/residual/velocity,
pelvis kinematics, thigh velocity, tension, K/C/travel and steps/cost. Advanced is
collapsed by default. No Density/Softness/JiggleStrength controls are added.

**Compare G1 OFF/current surface** reconstructs native LOD0 once, retaining the
same current Shape, primary pose and active G0.6.2 Morph weights. Only G1 helper
translation is removed in the OFF reconstruction. It saves P50/P90/P95/Max/RMS,
five regional distributions and a camera-independent silhouette proxy under
project `Saved/VamDiagnostics`, and draws cyan OFF/magenta current for 15 seconds.
It excludes material WPO, cloth and GPU readback. This is an engineering probe,
not visual acceptance or a per-frame CPU skinning path.

## Scope limits

No chair/contact/hand compression, inter-glute or thigh collision, self collision,
volume-pressure solve, new fold/corrective generation, or active muscle inference.
The motion is a small translation network around G0.5, not a volumetric muscle or
fat simulation. Large unsupported pose discontinuities intentionally rebase.
Multi-character scalability still needs target-platform profiling; reported solver
timings exclude structural evaluation, animation, rendering and skinning.
Non-unit/nonuniform Actor/component scaling has not been validated; use the
supported Shape system for character proportions. External material deformation
and packaged executable gameplay are outside the current engineering evidence.

## Verified outputs

| Output | Content Browser folder |
|---|---|
| Primary: ordinary generation | `/Game/VamRuntime/R_acd9111a769d555e99335c99` |
| Secondary: formal Upgrade Runtime | `/Game/VamRuntime/R_c504130ea06ee34f59aef652` |

Each folder contains `BP_VamCharacter`, `RC_Runtime`, `DA_GluteJiggle` and existing
structural/corrective assets. Each character passed 17 UE tests (15 clean, 2 with
pre-existing world-cleanup warnings), plus 52 Python tests globally. Editor,
Game Development and Shipping module builds succeeded. Cook processed 838
packages: 831 cooked, 7 platform skips, zero errors/warnings.

Each character's 288 G0.6.2 surface rows match the previous evidence exactly.
Breast calibration, modal and jump reported values are identical. The generic G1
30/120 and 60/120 full-trajectory divergences are .003870354 and .000753921 cm.
Six settled native-pose comparisons have maximum error below 3e-13 cm.
