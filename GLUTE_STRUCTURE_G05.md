# G0.5 — Pose-conditioned Structural Glute Refinement

## Scope and compatibility

Implementation on `feature/jiggle/hip`, based on HEAD
`6f159ffed33029f623db8ed9a4141af71a471c21`. G0 region construction, donor compression,
source bone indices, helper topology, native Morphs and Breast V3 are retained.
This stage is a deterministic pose function. No Glute solver, velocity, integration,
inertia, damping, contact, active contraction or fold deformation is added.

New generated profiles explicitly use schema 2, refinement version 1,
`glute-structure-g05-v1`. The class defaults remain schema 1/refinement 0 because
Unreal can omit properties equal to their defaults when saving older assets.
Existing G0 profiles therefore retain their G0 evaluation. Use **Upgrade Runtime**
to generate an immutable new output rather than changing an existing bundle.

## Data and evaluation

`FVamHipPoseState` stores the primary pelvis and two femur component transforms,
Shape revision, pelvis tilt/yaw/roll, and two `FVamHipSidePose` records. Each side
stores femur-in-pelvis, femur-in-anchor, relative quaternion, signed flexion/extension,
abduction/adduction and external/internal rotation in radians. Debug text uses degrees.
Positive signs denote flexion, abduction and external rotation. Pelvis diagnostics
are relative to the imported anatomical frame in component space, not actor heading.

The builder derives the femur longitudinal axis from thigh-to-shin source landmarks.
The relative quaternion is decomposed into twist about that axis and remaining swing.
Swing rotation-vector coordinates provide flexion and abduction, with mirrored side
signs. This is an engineering coordinate convention, not a clinical Euler-angle standard.
The decomposition is intended for ordinary hip ranges, away from the 180-degree swing
singularity. Quaternions are normalized and hemisphere-corrected.

`FinalizeBoneTransform` snapshots all three **primary** bones before writing either
side's helpers. Shape/Morph → base animation → ActivePose → Pose/debug → IK/ground →
joint limits → rigid physical blend → Glute refinement → Breast V3 → native skinning.
Glute and Breast write disjoint helpers. G0.5 never reads helper output as pose input.
`HipPoseState` and `GluteStates` are reflected per-component diagnostics, also available
to a future G1 consumer. The shared profile contains no mutable instance state.

## Mathematical refinement of G0

G0's pelvis-to-femur fiber scaffold remains the foundation. Its bounded logarithmic
fiber stretch has axial scale `exp(s)` and transverse scales `exp(-s/2)`, so each
regional affine scaffold has determinant one. G0.5 adds calibrated regional routing:

- `F = positive(flexion)^2 / (1 + positive(flexion)^2)`;
  extension uses the same smooth feature with a 0.6 radian scale.
- `A = tanh(abduction / 0.7)`, `T = tanh(axial rotation / 0.7)`.
- A regional support load is the dot product of stored gains and `[F,E,A²,T²]`.
- Passive tension combines G0 positive fiber log-strain squared with a bounded
  pose-routing strain proxy squared. It does not estimate active muscle activation.
- Pelvis and thigh support shares are independently adjusted and normalized.
  A regional AP/ML/SI support baseline is emitted for future dynamics, not simulated.
- Position combines the G0 scaffold offset, femur attachment displacement and
  dimension-scaled semantic response. Per-axis `tanh` travel limits are continuous;
  downward SI travel has a separate bound with matching first derivative at zero.
- A smooth posterior barrier preserves a pose-dependent fraction of the imported
  regional projection. It is expressed in the pelvis anatomical frame.
- Orientation combines constrained G0 fiber bending and semantic pose response.
  A smooth rotation-vector norm cap limits each region's rotation; there is no
  freely rotating whole-glute joint.

The resulting transforms define `FinalRestGlute`. Regional mass-center transforms
and existing geometry-derived mass fractions produce `FinalRestCOM`. No DeltaTime
or previous structural state enters the function.

| Region | Structural responsibility |
|---|---|
| Core | Strong pelvis support and posterior retention; modest femur follow and flexion support increase. |
| Upper | Stronger pelvis tether, small downward travel, constrained orientation. |
| Lower | Larger femur follow; flexion/extension alter the glute-thigh transition while retaining bounded support. |
| Medial | Strong sacral tether, smallest transverse travel and orientation range. |
| Lateral | Regional abduction/rotation response and femur/fascial routing proxy. |

The coefficients are shared semantic engineering policy, calibrated by G0's measured
attachment shares and dimensions, then stored in each profile. They are not measured
muscle constitutive parameters. There are no character/preset-specific branches.
Left and right use their own geometry and inputs; symmetry is used only for coordinate signs.

Local determinant preservation and projection bounds establish structural responsibilities;
they do **not** prove global skinned surface volume conservation or visual anatomy.

## Shape, helpers and automatic generation

The existing `GluteAnchor → Core/Upper/Lower/Medial/Lateral` hierarchy is unchanged.
Each anchor remains under pelvis. Original LGlute/RGlute remain intact. No additional
G0.5 bone or influence is required; G0's continuous region and donor compression remain.

The builder now additionally saves the imported pelvis reference, femur axis, regional
pose coefficients and Fold semantic map. Runtime recipe algorithm identity is bumped
to `runtime-bundle-v8-glute-pose-refinement`, so generation/upgrade publishes a new
committed output. Resource Browser still has one **Generate UE5 Character Asset** flow.

Shape previews evaluate from a fresh per-instance profile copy, apply existing morph
responses, recalibrate geometry-dependent regional coefficients and Fold references,
then update the instance reference-pose override. No shared Skeleton mutation or new
helper identity is involved. Preview and commit do not inject motion or require rebase.

## Fold semantics

`FVamGluteFoldSemanticMap` contains medial infragluteal anchor, middle transition and
lateral fade coordinates derived from Lower/Medial/Lateral geometry. Stored activation
gains are calibrated from regional pelvis/thigh shares. `FVamGluteFoldState` emits three
bounded activation factors and three flexion stretch factors. Extension and rotational
routing can increase activation; flexion changes stretch. These values are only semantic
outputs. They never add a fold to vertices, bones or Morphs.

## Debug in an ordinary Empty Level

1. Generate a character or select an existing generated BP and use **Upgrade Runtime**.
   Use the new BP output. Put it in a new Empty Level and Play/Simulate.
2. Open the existing VaM character debug panel, select the runtime character, expand
   **Glute Structural Debug - G0.5** below Breast controls.
3. `Enabled` toggles structural evaluation. `Show Glute Region` shows region evidence;
   `Show Structural Bones` shows the scaffold; pelvis/thigh attachment toggles draw
   their networks. `Show Pose Tension` labels regional passive tension.
   `Show Fold Semantics` draws the three reserved points in magenta, not a visual fold.
4. Choose `Target both`, `Target left` or `Target right`. `Neutral standing` clears the
   selected thigh debug offset; `Reset` clears both. Flexion requests 70°, extension
   −20°, abduction 30°, adduction −20°, external/internal rotation ±30°. Normal joint
   limits still apply. Buttons add thigh pose offsets; they are not a balanced bending
   animation and do not replace a running base sequence or other pose controls.
5. Diagnostics show signed hip angles, pelvis tilt/yaw/roll, Shape revision, effective
   volume, rest/final COM, dimensions, original/current attachment shares, regional
   position/orientation changes, passive tension, AP/ML/SI support and Fold state.
6. For bending, use a pelvis/femur animation or existing pose controls. Pure actor
   rotation with unchanged pelvis-femur relation intentionally produces no new
   structural deformation. Use the existing clothing visibility toggle if clothing
   obscures observation; clothing assets remain available.

## Verification and limitations

See `Evidence/GluteStructureG05/` for the actual build, automation, asset and cook results.
Tests check engineering invariants, not visual appearance. New tests cover neutral bind,
regional response, mirror positions, combined pose sweeps, bounded offsets, determinant,
frame-count independence, snapshot serialization, primary-input isolation, asymmetric
side input, Shape continuity and multi-instance state. Existing native tests continue
checking source indices, influences, Morphs, actual weighted mesh and persisted profiles.

Current support is VamFemale88. Shape responses remain bounded first-order geometry
approximations. The model does not solve full muscle mechanics, exact global volume,
extreme hip singularities, active contraction, final fold/wrinkle corrective, clothing,
sitting compression, collisions or G1/G2. Human inspection of region boundaries,
contours and pose transitions remains necessary. No visual acceptance is asserted.

## Completed verification

Editor, Game Development and Game Shipping builds succeeded. Each of two character bundles passed 10/10 Glute/Breast tests; installed-project replay passed 10/10; the legacy G0 NativeRuntime test passed. Two NativeRuntime tests carry existing missing-EndPlay teardown warnings. Windows cook completed 834 packages with zero errors and warnings. See [implementation report](Evidence/GluteStructureG05/implementation-report.md) for exact assets and evidence.
