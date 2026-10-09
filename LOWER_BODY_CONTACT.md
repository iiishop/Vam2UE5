# Lower-body volume contact — first implementation

## Scope

Branch: feature/chaos/hip. Reuses the existing UE fTetWild authoring path and custom RDG GPU contact backend. This is NOT Epic's stock Chaos Flesh solver executing on the GPU. Breast, glute and leg Jiggle remain independent; contact publishes an additional residual displacement after native skeletal animation.

The goal is broad rigid-surface compression of glutes, thighs and calves with volumetric resistance and lateral spreading. This revision does not implement detailed grasping, anatomical tissue segmentation, automatic sitting balance, body-weight allocation or lower-body reaction forces to the skeleton/rigid seat. Existing breast reaction feedback is retained; lower regions are deliberately excluded from its chest-specific response mapping.

## Authoring

`VamLowerBodyContactBuilder::Append` consumes matching GluteStructure and LegJiggle profiles and the final refined native body. It combines existing source region weights per side. No character IDs, presets or world coordinates are used.

Source surface triangles form the outer boundary. Coincident points are welded for the PHYSICS envelope so UV seam duplicates do not create internal caps. An inner support surface is derived from pelvis/thigh/shin/foot bone segments, with radial fraction 0.45; this is a geometric engineering proxy, not MRI-derived anatomy. Open patch edges are closed to that inner surface. Inner particles are fixed or weakly supported; outer particles retain source skin/helper influences. Support weights are normalized.

UE fTetWild parameters: edge 0.10, epsilon 0.0005 relative to envelope bounds, 40 iterations, coarsening enabled. Per-side safety budget: 12,000 particles / 60,000 cells. Failure is explicit and does not mutate the input profile. These are asset-build limits, not a runtime performance guarantee.

Four named volume regions: LeftBreast, RightBreast, LeftLowerBody, RightLowerBody. Schema 5 retains the legacy `BreastContact` configuration property for compatibility. Lower parts share a per-side region identifier; disconnected source support patches are not claimed to be anatomically continuous. Source Morph deltas and render bindings are regenerated. Skeletal bone indices and render topology are unchanged by this append step.

## Runtime

- Variable region counts replace the old GPU bilateral indexing assumptions.
- Existing material, compression/inversion barriers, skin bending and region-volume correction operate on the added cells.
- Final animated pose drives the support targets; no extra gravity or second Jiggle solver is added.
- GPU world contact accepts spheres, boxes, capsules and supported convex shapes. Nonuniformly scaled axis-aligned simple boxes are supported for ordinary scaled Cube seats. Other nonuniform/complex/instanced collision remains unsupported.
- Shape-dependent wake bounds are cached in bone-local space; per-frame broadphase wake work scales with support bones.
- Lower body requires the GPU backend. Unsupported cases report an error rather than entering the old bilateral CPU path.
- Disabling lower contact freezes its particles and masks its residual. The combined GPU topology is still present if breast contact is active: this switch is not a promise of zero lower-region dispatch cost.
- Global `SetContactEnabled(false)` releases the solver and leaves Jiggle intact.

## User controls

Generate UE5 Character Asset now appends lower contact after breast render refinement and builds the shared deformer. Existing generated characters need a newly generated runtime; an existing BP does not gain new cage data merely by updating the DLL.

Debug panel: `Body Volume Contact · 胸部 / 臀腿按压与体积` → `Lower body volume` toggles lower residual deformation. Actor Blueprint call: `SetLowerBodyContactEnabled`; component call has the same name. Global contact enable remains the master switch.

`Leg Jiggle` pose controls include `Seated`: bilateral 90-degree hip/knee pose with foot-height compensation. This is an observation pose, not a sitting controller. Place a normal Cube with simple blocking collision as the seat and move it gradually into the tissue. Calf compression can be observed with another broad cube against the calf. Avoid spawning a deeply penetrating collider. Enable cage display to inspect contact and named region volume diagnostics. Shape/pose inversions are rejected rather than published.

## Limits and evidence interpretation

A generated cage and a successful compile do not prove seated-pose stability, visible spreading, or a frame-rate target. This revision has not established seated compression performance or visual acceptance. The source-region masks may leave sparse transitions; the shared region identifier alone cannot fill missing source support. Material coefficients remain an engineering approximation. Regional volume conservation is approximate and must be judged together with local Jacobians/penetration, not only total volume.

No source assets are overwritten or deleted. Candidate creation uses a new Content folder and requires independent reload before publication.

## References

- Epic [Chaos Flesh Quickstart](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart): volumetric tetrahedra, skeletal kinematics/weak constraints, simple rigid contact and surface bindings. Its documented stock world coupling is one-way; this project has a separate GPU implementation.
- Epic [Chaos Flesh Overview](https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview).
- Local UE 5.8 `GeometryAlgorithms/Public/FTetWildWrapper.h` defines coarsening, tolerance, edge-length and filtering options used here.
## Build delivered 2026-10-09

- Editor Development and Game Development builds succeeded; logs in `Saved/LowerBodyContact/final-UnrealEditor.log` and `final-UnrealGame.log`.
- Candidate: `/Game/VamRuntime/LowerContact_20261009_v5/BP_VamCharacter`.
- Configuration: `/Game/VamRuntime/LowerContact_20261009_v5/RC_Runtime`.
- Combined profile: `/Game/VamRuntime/LowerContact_20261009_v5/DA_BodyContact`.
- Total cage: 9,200 particles. Lower-left 3,859 particles / 11,760 cells; lower-right 3,961 particles / 12,126 cells. Effective lower envelope volumes 7,647.60 / 7,637.24 cm3 (geometry proxy, not a measured anatomical tissue volume).
- Same-side thigh/calf self-contact is not represented separately by the shared lower-side region; deep knee-fold contact is outside this first revision.
- No new automation suite, seated runtime benchmark, video or visual acceptance was run for this request. Compilation/asset reload must not be presented as proof of the final seated result or 60 FPS.

Independent asset reload and publication succeeded: `Saved/LowerBodyContact/candidate-reload.json`. Installed Editor binaries match the final host build. Source changes are uncommitted.
