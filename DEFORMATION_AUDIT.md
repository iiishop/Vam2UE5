# A1 deformation audit

Select any loaded BP_VamCharacter instance (Editor or PIE): Details → VaM / Diagnostics → Export Deformation Audit. The same function is Blueprint callable. DeformationAuditStatus contains the unique output directory under project Saved/VamDeformationAudit.

The capture briefly suspends this actor's component ticks until the native render readback completes (15 second recovery timeout). It does not change assets, material, solver parameters, or skinning mode. Large JSON exports and synchronous cage readback are explicit diagnostics, never normal frame work. Capture is not a performance benchmark.

## Data contract

- manifest.json: actual loaded runtime configuration/build identity; requested component settings; profile paths/versions; effective deformer selection; actor/world/frame/load generation; body-to-world matrix; active morph buffer; shape values; runtime and editor cameras.
- mapping.json and source-mapping.json: render → input → source, including recursive midpoint provenance for newly refined vertices. Source -1 is never silently mapped by position.
- stage inputs.json: exact render-domain bone indices/weights, component matrices, inverse bind matrices, active morph weights, triangle indices. preskin.json includes weighted morph positions before skinning.
- 00-source-lbs: original definition resolved via retained provenance or receipt chain, mapped by primary bone name into this captured pose. Missing provenance is an explicit error, never replaced with final weights.
- 01-final-weights: generated weights with shape-reference helper transforms. This is explicitly NOT the source baseline.
- 02-static-helpers: same primary pose and shape; static leg attachments and Glute Structure evaluation; no corrective or dynamic offsets.
- 03-corrective: static helper pose plus the actually active corrective weights.
- 04-jiggle: captured final component matrices, including current dynamic offsets.
- cage-canonical / shaped-reference / animated-target / current: distinct arrays in body component cm. The target/current pair is synchronously read from the published resident buffers, not the periodic diagnostic cache. Its input frame is retained; cage_matches_capture_frame exposes stale input rather than hiding it.
- cage-topology: tetrahedra, particle weights/regions, render parents/barycentric weights/masks.
- 05-gpu-surface: UE native Mesh Deformer geometry readback. A separate status distinguishes pending/unavailable/readback; no CPU substitute is labelled GPU output. Engine support is editor-only (including PIE).

Stages are fixed-pose layer decompositions, not six fresh dynamic simulations. Disabled features stay disabled. Stage 04 preserves the actual dynamic pose. All stage outputs share body space and recorded cameras. These diagnostics do not claim the shape is anatomically correct.

## Replay

`python Scripts/replay_deformation_audit.py <absolute bundle directory>`

Requires numpy. Reconstructs each stage from captured preskin vertices, matrices and weights, writes OBJ files, checks replay error, and records SHA256 hashes in replay-report.json. When GPU geometry exists it also compares native output against LBS plus embedded cage residual. The saved camera/world matrices allow identical projections across stages.

For OFF/ON experiments use Reset Deformation Audit State after each switch and before sampling. It releases contact history, resets all Jiggle solvers and refreshes morph buffers while retaining shape and pose. Do not interpret uninitialized contact (no cage buffers) as a zero-deformation successful solve.

New runtime builds retain DiagnosticSourceDefinition outside editor-only receipt data. Legacy assets are read through their receipt chain without modifying them.


## B1: corrective depends on final skinning data

The runtime pipeline now builds Breast, Glute structure, and Leg helpers/weights first, then contact render refinement, then `BuildCorrective`. The retained pre-Glute definition supplies original influences; they are never reconstructed from the final weights. Refinement ancestry transfers those original influences deterministically. Generated vertices without source IDs have separate weld identities.

The final bake reads quantized render-buffer weights. Adding morphs may reorder UE render vertices: the final shape mapping and contact surface bindings are remapped through retained input IDs before lower-body cage generation. Authored-pose verification runs again on the resulting render topology and actual weights, with per-target maximum delta error and triangle safety checks. Neutral-bind reconstruction is not the acceptance criterion.

`SealCorrective` records data digests for final/source weights, bind, topology, morphs, helper definitions, shape/mapping, original source corrective data, and actual corrective policy/targets. Character loading rejects missing or mismatched seals with a rebuild message. Legacy unsealed profiles are deliberately not silently accepted. Build/reload must both pass `ValidateCorrective`.

The opt-in `Vam.Glute.FinalCorrective` automation test takes `-VamFinalCorrectiveConfig=<RC path>` and optional `-VamFinalCorrectiveReport=<json path>`. It replays all authored targets, changes one actual weight by one quantization unit to confirm rejection, changes helper data to confirm rejection, then restores and revalidates. It does not save the mutations.

### B1 measured acceptance (2026-10-09)

- Rebuilt bundle: `/Game/VamRuntime/R_251e666f091799716ce873fe/RC_Runtime`; separate build, reload, and publication verification processes succeeded.
- Final render weights use 16-bit precision, preserved across refinement/extraction/rebuild. Bake matrices use their decoded values without a second normalization/quantization pass.
- `Vam.Glute.FinalCorrective`: passed 20 non-neutral side/target cases; maximum replay error `0.0001477696093890908 cm`, zero unsafe triangles. The existing `0.001 cm` acceptance limit was not increased.
- Changing one actual weight unit reports `final_weights`; changing helper data reports `helper_definitions`. Restoring the data restores seal validity.
- Evidence: `Saved/LowerBodyContact/B1/authored-targets.json`, `acceptance.log`, and `bundle.json`. This proves final bake/operator consistency, not anatomical fidelity or contact-cage validity in every pose.

- Recaptured 18 states / 288 PNGs with the new BP. All A2/B1 cameras and local bone matrices match exactly; suspended-seat shape parameters match. Seated contact remains blocked (`Current pose invalidates contact rest volume`); lower glute/thigh folding remains visible.
- B1 binaries installed after acceptance, hashes matched to isolated build; previous binaries retained in `Saved/LowerBodyContact/BeforeB1Install`.
