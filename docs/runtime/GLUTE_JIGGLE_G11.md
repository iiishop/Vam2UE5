# G1.1 — Glute reference-gravity equilibrium

## Subsequent user-selected amplitude default

The user requested three times the visible Hip strength after the G1.1 delivery.
`GluteAmplitude` now defaults to 3.0 and multiplies the final helper secondary
translation (including reference-gravity equilibrium offsets). Setting it to 1.0
restores the previous amplitude. Imported structural rest, physical solver state,
mass, damping, support, travel calibration and Breast are unchanged. This is a
presentation gain, not three times the physical force. Debug nodes and velocity
arrows follow the displayed gain; diagnostic solver offset/travel remain unscaled.
The panel exposes Amplitude from 0 to 10; snapshots record `amplitude_scale`.
Previously recorded G1.1 numerical/surface evidence below predates this gain and
must not be presented as a new three-times-amplitude test run.

## Contract

G0.5/G0.6.2 imported appearance is the authored 1g equilibrium. Each side stores
`ReferenceGravityLocal` in the same anatomical anchor axes as dynamic node Rest.
The builder transforms `(0,0,-980)` cm/s² by the inverse rotation of
`Side.AnchorLocal * StructureProfile.RestPelvisComponent`. Runtime Shape and hip
pose recalibration copy this immutable vector; they never re-author it.

For current anchor world orientation Q:

```
currentLocal = Q^-1 * worldGravity
residualLocal = currentLocal - referenceLocal
preloadWorld = -Q * referenceLocal
nodeForce = nodeMass * (worldGravity + preloadWorld)
```

The old schema canceled current world gravity at every step. Schema 2 instead
rotates the reference preload with the body. Default authored orientation at 1g
has zero residual; whole-body pitch/roll or gravity magnitude changes create a
load. Thigh-only pose changes do not change this gravity vector. Current G0.5
support/tension and the existing nonlinear network determine the response.

World particles already account for moving-reference inertia. No additional
angular/linear inertial forces, particle modes, per-frame CPU skinning, morph
rebuild or contact system were added. Breast solver code remains unchanged.

## Versioning and generation

- New profiles: schema 2, `glute-dual-attachment-g1.1-reference-gravity-v1`.
- Gravity policy: `body-attached-imported-1g-v1`.
- Schema 1 class defaults deliberately remain unchanged for old serialized assets.
  Their legacy cancellation remains active, with an explicit Upgrade Runtime warning.
- Generate UE5 Character Asset and Upgrade Runtime both call the updated builder.
  Source/algorithm fingerprints produce a new immutable runtime output.
- Existing source assets, runtime folders and scene instances are not overwritten.

Default tuning stays Support 0.45 / Damping 0.65 / Mobility 2.0 /
Internal Coupling 1.0 / Mass Scale 1.0.

## Authorized near-limit correction

Real-character testing exposed a separate limit issue: the original finite
hardening curve could have insufficient restoring force at the emergency hard
stop to balance ordinary supine gravity (1,136 cumulative left-side corrections
in the first candidate). The user explicitly authorized correcting this curve
while preserving tuning, mass, travel and skinning.

Let q = abs(displacement)/directionalTravel, s = SoftLimitFraction, and
u = max(0, (q - (1+s)/2) / ((1-s)/2)). Add a barrier stiffness
`baseSupport * LimitHardening * u²/(1-u)` only beyond `(1+s)/2` of travel.
It joins with zero value and zero first derivative. Its force grows approaching
the existing hard boundary. No travel limit or user parameter is increased.
The original interior force is retained. A bounded Newton solve with analytic
diagonal barrier tangents and a feasible line step solves the additional force
implicitly; simply lagging this stiffness caused numerical oscillations and was
rejected. Legacy schema 1 retains its original curve and gravity behavior.

World integration remains the existing fixed 120 Hz backward-Euler formulation.
It has first-order position error against an analytically accelerating anchor.
Freefall/zero-g comparison therefore uses bounded trajectory error and physical
step convergence, not a claim of exact accelerated-frame invariance.

## Manual inspection in any ordinary level

1. Place the newly generated BP_VamCharacter in an Empty Level, then Play/Simulate.
2. Select the running character in VaM character debug and expand Glute Jiggle.
3. Gravity Default uses the current scene gravity. Gravity Zero/Half/Double set a
   transient override for this selected character's **Glute solver only**. They do
   not modify WorldSettings, Breast, actor movement gravity or saved project settings.
4. Rotate Character 90 Pitch / Roll smoothly rotates the whole actor over one
   second relative to the orientation captured when first using these commands.
   Reset Orientation smoothly restores that captured orientation. No collision is
   modeled; inspect in free space, without interpreting the result as chair/floor contact.
5. Compare standing, pitch (supine direction depends on the actor's facing), and
   roll. Wait for settling, then use the existing G1 OFF/current-surface comparison.
6. Use Gravity Zero, wait, then Gravity Default to observe force transitions and
   settling. Neither gravity command clears particle displacement or velocity.
7. Apply Flexion 90 in Glute Structural Debug, then rotate the actor to side lying.
   Residual gravity should match neutral at the same pelvis orientation; the
   equilibrium can differ because the regional support/tension differs.

Diagnostics show world/current-local/reference-local/residual-local gravity in
cm/s², total GravityForce/ReferencePreload in kg cm/s², residual magnitude,
node state, step count and emergency hard-limit corrections. Residual magnitude
is a gravity-load diagnostic, not gravity minus measured actor acceleration.

## Scope

This is reference-gravity correction for the existing five-particle dual-support
system. It does not infer anatomy accuracy, visual naturalness, volume-preserving
soft tissue or contact. No Thigh Jiggle, Chaos, sitting, compression, collision,
new corrective or G0.6.2 geometry changes are included.

## Verified delivery

Editor Development, Game Development and Game Shipping builds succeeded.
Python tests: 52 passed. Both characters: 20 UE tests passed each (18 clean,
2 existing temporary-world cleanup warnings). Independent reload/publication
verification succeeded, with 103 algorithm hashes matching final source.
Windows cook: 831 cooked + 7 platform skips, zero errors/warnings.

Current outputs:

- Generate: `/Game/VamRuntime/R_732d900601c0c230e0980b1b`
- Upgrade: `/Game/VamRuntime/R_0bcc2602fdc30be9a7cf8028`

Each contains `BP_VamCharacter`, `RC_Runtime`, `DA_GluteJiggle`.
Full numerical results, limitations and manual steps:
[verified Chinese report](../../Evidence/GluteJiggleG11/REPORT_ZH.md).
Machine-readable evidence: [summary](../../Evidence/GluteJiggleG11/summary.json).
