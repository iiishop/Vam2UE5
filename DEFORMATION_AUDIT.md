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

## B2: corrective attribution

`VamGluteStructureBuilder.ExportCorrectiveDiagnostics` (editor/Python/Blueprint) validates the final-data seal, prepares the same actual weights and original influences as B1, and reruns the existing G062 builder into a transient profile. It refuses to publish diagnostic metadata if this decomposition differs from the sealed authored targets by more than 0.001 cm. It never saves or replaces the source mesh/profile.

Use `Scripts/run_corrective_diagnostics.py --project <uproject> --configuration <RC path> --blueprint <BP generated-class path> --output <new directory>`. This exports the bake fields, runs the actual BP pose suite, and generates a report. For an already captured suite, `Scripts/report_corrective_diagnostics.py <output> --suite <suite directory>` replays without UE. Requires numpy, matplotlib, scipy and Pillow.

- `fields/detail.json` identifies the actual definition/profile/data seal, authored interpolation targets, and all 72 safety probes. Synthetic probes (including 130-degree combinations) are explicitly NOT VaM captured shape targets. Verified Bend Fix/thigh-X source provenance does not turn every interpolated target into an independent VaM reference.
- `.f32` fields are little-endian float32 in full input-vertex order. Raw/adapted source uses anchor-local cm; projected source/procedural/rotation residual/final fields use pre-skin cm with separate axis bases. Before fields are after their respective masks/limits/smoothing, before safety. Safety scalars accumulate the actual factors from target, blended-pose and residual conditioning, rather than estimating safety from a final RMS ratio.
- The existing instance `Export Deformation Audit` now includes `corrective_runtime`: enabled state, actual swing-twist degrees, all native G062 target weights, and requested basis weights. Captured `active_morphs` is authoritative for reconstructing the actual output. No RBF or swing-twist implementation is duplicated in Python.
- `components.npz` per pose contains separate posed source/procedural/residual/final displacement arrays, before-safety arrays, the actual final morph delta, baseline/final surfaces, render mapping/triangles and ROI masks. `targets.csv` contains every target, including inactive targets, raw-source magnitude, actual axis weights and per-region exact safety factors. `regions.csv` includes local extrema/worst vertices, coverage and cancellation.
- Inspection ROIs cover anterior hip, lateral glute and glute-thigh transition independently of the corrective mask, so uncovered tissue stays visible. These are engineering Gaussian ROIs, not anatomical truth. Definition/widths are recorded in detail.json. Numerical statistics deduplicate source aliases and include zero-displacement vertices.
- Plots are explicitly diagnostic projections. Actual UE captures are separate. `regional-contours.json` measures rasterized triangle-union boundary changes in each captured camera/ROI bounding box; occluded regions can have zero visible silhouette change despite vertex displacement. Resolution is the captured 768 pixels, not a subpixel claim.
- Heatmap magnitude-loss proxies include cancellation and must not be read as exact safety factors. Exact per-target factors are in the table/CSV and `.f32` arrays.

### B2 measured results (2026-10-09)

Evidence: `Saved/LowerBodyContact/B2/compare.html`. Same sealed B1 asset, no anatomy/physics parameter changes. All 18 poses / 288 native images recaptured. Recomputed targets differ from sealed targets by at most `1.5856872636705748e-14 cm`; decomposition vs actual captured final morph differs by at most `0.00012228045660803334 cm`.

In suspended-seat, actual left/right Flex90 target weights are `0.9999837982 / 0.9999852658`. Anterior hip engineering ROI mean masks are about 0.0192/0.0193, with weighted mask>0.001 coverage 17.8%/19.5%. In the lateral ROI source contribution is zero; procedural maxima are 0.335/0.402 cm, residual maxima 2.877/3.066 cm. This distinguishes missing source coverage from failure to activate a target. Safety attenuation exists, particularly in the residual; it is not the sole explanation. Seated contact remains blocked by invalid posed rest volume.

B2 diagnostics binaries compiled and installed; all 7 installed DLL/PDB/module hashes match the isolated build. Previous binaries are in `Saved/LowerBodyContact/BeforeB2Install`. Regional contour report contains 216 camera/ROI measurements; suspended-seat anterior-hip side contour changes 0 pixels while the glute/thigh contour changes visibly.
