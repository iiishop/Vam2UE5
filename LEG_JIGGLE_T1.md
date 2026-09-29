# Leg Jiggle T1 — thigh and calf

Branch: feature/jiggle/thigh. Engineering implementation; visual acceptance belongs to the user.

## Research basis

- Wakeling & Nigg (2001), Modification of soft tissue vibrations in the leg by muscular activity: https://journals.physiology.org/doi/full/10.1152/jappl.2001.90.2.412 . Leg tissue vibration frequency/damping varies with muscle state and direction.
- Wakeling, Nigg & Rozitis (2002): https://journals.physiology.org/doi/full/10.1152/japplphysiol.00142.2002 . Muscle activity damps tissue resonance. Pose alone cannot measure that activity.
- Passive uni/biarticular structure identification (2023): https://pmc.ncbi.nlm.nih.gov/articles/PMC10310719/ . Joint combinations matter for passive muscle-tendon length.
- OpenSim developer documentation: https://opensimconfluence.atlassian.net/wiki/spaces/OpenSim/pages/53090555 . Passive force-length behavior is distinct from active excitation.
- Stanford passive calibration developer notes: https://github.com/stanfordnmbl/PassiveMuscleForceCalibration/blob/main/README.md . Generic passive models need calibration, particularly at extreme poses.

These sources motivate the architecture, not a claim that family coefficients are experimentally fitted. Frequencies of a muscle-fat surface composite are engineering defaults (4–8 Hz transverse, 1.35 times longitudinal), not measured individual muscle properties.

## Implementation

`UVamLegJiggleProfile` stores immutable family/topology provenance, four segment calibrations, Shape responses and numerical settings. `UVamLegSkeletalMeshComponent` owns four independent five-node solver states. `VamLegJiggleBuilder` automatically appends 24 bones: each L/R Thigh/Calf has an Anchor and five distributed tissue helpers. All existing source, Breast and Hip bone indices remain unchanged.

Thigh semantics: RectusFemoris, Hamstrings, Adductors, VastusLateral, VastiCore. Calf semantics: GastroMedial, GastroLateral, Soleus, TibialisAnterior, Peroneal. These are surface influence regions inspired by anatomy, not segmented internal muscles.

Region = original thigh/shin skin ownership, smooth longitudinal knee/ankle fade, Hip region exclusion, seam-aware topology diffusion. Continuous normalized spatial kernels distribute helper influence. Only the owning thigh/shin weight donates, with transfer capped at 65%; original support remains. Reuses Breast donor compression and eight-influence handling. Builder checks original bone transforms/indices, hierarchy, normalized influences and zero-offset bind reconstruction.

Effective volume = sum of triangle surface area × region weight × radial distance / 2. This is a dynamics proxy, not medical volume. Node masses derive from regional volume × density. Regional COM and radius come from the same weighted surface. Body directions come from pelvis/abdomen, bilateral thighs and foot/toe landmarks.

## Pose-dependent passive response

Source pelvis/thigh/shin/foot transforms are sampled before helper writes. Joint orientation changes use shortest quaternion logarithms projected onto anatomical reference axes. This is a bounded joint-angle surrogate, not a full anatomical joint decomposition at extreme rotations.

For each region, log-length change = family hip/knee/ankle coefficients · joint angles + family abduction/rotation terms. Coefficients scale with that segment radius/length. Positive passive strain is max(0, exp(clamped log-length) - 1 - slack). Tension = 1-exp(-12 strain²). Region support and damping increase continuously with its tension. Shortening returns to baseline; it does not imply active contraction.

Rectus femoris: knee flexion stretches, hip flexion shortens. Hamstrings: hip flexion stretches, knee flexion shortens. Vasti primarily respond to knee flexion. Gastrocnemius combines knee extension and ankle dorsiflexion; soleus responds primarily to dorsiflexion. Anterior calf responds to plantarflexion. Regions and sides evaluate independently.

## Dynamics and pipeline

Reuse the existing Glute five-node world-particle numerical kernel, without copying Breast angular mode or altering Hip calibration. Fixed 120 Hz implicit coupled translation integration, anisotropic springs, moving primary/distal damping attachments, reference-gravity preload, continuous near-limit hardening, teleport/pause/rebase policies. Primary frame is the actual final thigh or shin pose. No independent free whole-leg rotation.

Leg helper writes precede existing Hip/Breast finalization and use disjoint bones. Native skeletal GPU skinning remains the visible path. Per-instance histories, node positions and accumulators are never stored in the shared profile.

Shape changes apply precomputed region rest-position and logarithmic volume responses, preserving helper indices and rebasing invalid motion history. Volume response is a first-order morph approximation using original surface quadrature; it is not a recomputed volumetric anatomical model.

Generate UE5 Character Asset automatically adds Leg analysis, helpers, weights, DA_LegJiggle, and RuntimeConfiguration reference. Upgrade uses current family mapping and writes a new immutable runtime output.

## Controls

VaM character debug panel → `Leg Jiggle · 大腿 / 小腿`:

- Enabled; Show Leg Nodes; Show Leg Region (reference samples); Show Passive Tension.
- Thigh Amplitude and Calf Amplitude: visible offset multiplier, default 3, range 0–10. This is an artistic skin displacement multiplier; solver limits apply before this multiplier.
- Support and Damping: independent numerical controls, default 1.
- Neutral, Hip Flexion, Knee Flexion, Crouch, Dorsiflexion, Plantarflexion, Reset: set source joint debug poses.
- Motion buttons reuse the existing actor acceleration/stop/jump/walk commands.
- Diagnostics show angles, effective mass/volume, gravity residual, solver steps and regional passive tension/displacement.

Use a newly generated BP in any ordinary level, start Play/Simulate, select it in the debug panel. Hide clothing with the existing clothing toggle if needed. Static pose changes tension; actual movement supplies inertia. Compare the same movement in different poses. Region dots show reference samples transformed by segment frame, not exact live skinned vertex positions.

## Scope and limitations

No EMG/activation input, active contraction inference, tendon wrapping, internal anatomical muscle segmentation, contact, cloth, Chaos, or tissue collision. Regional anatomical names and coefficients are approximations. Large artistic multipliers can expose intersections. Clothing retains its original skinning and does not automatically receive new flesh weights. No visual acceptance claim.

## Delivery evidence (2026-09-29)

Base HEAD `b290696f6db3bddffcc7d926cdeadd330defdb28`; implementation remains uncommitted on feature/jiggle/thigh. Editor and Game Development builds succeeded; five Editor binary/module files installed with matching hashes.

- Primary: `/Game/VamRuntime/R_71e92f46b598d978655ae588/BP_VamCharacter.BP_VamCharacter`; DA_LegJiggle in the same folder. Build, independent reload and publication completed.
- Secondary: `/Game/VamRuntime/R_977282d4e78b8e3d3fc1a8b7/BP_VamCharacter.BP_VamCharacter`; DA_LegJiggle in the same folder. Build, independent reload and publication completed.

Generation includes intrinsic profile/hierarchy/weight/bind checks. No automated dynamics suite, cook, or visual acceptance was run in this task. Logs and completion manifest: `Saved/LegJiggleT1/`.
## T1 seam and pose correction

- UV/material split copies of a source vertex now share one final influence map; region values are also reduced over the logical vertex. Adjacency diffusion alone was insufficient to guarantee closed seams.
- Debug presets stop the walk trajectory, clear previous debug/pose offsets and release foot locks before setting a pose. Hip/knee/ankle probes use the left leg, retaining the right support leg.
- Crouch uses matched 45 degree hip/knee flexion and a geometry-derived root translation that retains the neutral foot positions. Ankle remains neutral so the feet remain level in the reference sagittal model.
- Joint axes use the current Shape reference parent rotations. The pose controls remain observation tools, not an authored animation or a balance controller.
- New algorithm identity: leg-pose-tension-t1-v2-seams. Existing generated meshes must be upgraded/rebuilt because their skin weights are stored in the asset.

Fix delivery: Editor and Game Development compilation succeeded; updated Editor DLLs installed with matching hashes. New committed BP: `/Game/VamRuntime/R_9345f3209950f77911bda5eb/BP_VamCharacter`. Intrinsic builder log: 2012 coincident source-vertex copies, maximum incoming representative weight difference 0.344365597; after region/weight generation, 0.000000000. Build, independent reload and publication all exited 0. These are asset-generation diagnostics, not visual acceptance. Logs: `Saved/LegJiggleT1/PrimarySeamFix/`.

## T1 ring-down refinement (2026-09-29)

The old regional damping ratios (typically 0.35, anterior calf 0.5) dissipated motion quickly; backward Euler at 120 Hz added numerical damping. Runtime now uses a per-instance copy of integration settings at 240 Hz, retaining the original hitch time budget (normally 32 substeps). Shared profiles and Hip/Breast integration are unchanged. Existing generated Leg profiles work without regeneration.

Defaults: thigh output amplitude 3, calf amplitude 3, support 0.85, global legacy damping 1, new thigh damping multiplier 0.30, calf damping multiplier 0.40. Panel exposes the two regional damping controls separately; smaller values sustain motion longer. Passive pose tension continues to increase regional support and damping. These controls reduce damping rather than multiplying visible amplitude again. The effective solver damping multiplier is the global multiplier times the segment multiplier, clamped by the existing solver to 0.1–4.

A new Restore Leg Defaults button resets these controls and solver history without changing pose. Diagnostics report 240 Hz and current damping multipliers. Substep work is approximately twice the prior configuration at the same rendered frame rate; no performance benchmark or visual acceptance is claimed.

Additional Shape correction: recompute COM and attachment points from the changed rest geometry, update radial dimensions, and scale node coupling stiffness consistently with node support. Previously these remained at imported values after Shape changes.

The thigh branch fast-forwarded to e4f721a to preserve the previously completed Breast amplitude=2 feature. No runtime asset rebuild or new test character is required.
