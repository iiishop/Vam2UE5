# Knee compression round 2 — not accepted as anatomical compression

Base: 9a69057. New candidate 6: 489612797e50733ba5dce8ee91e588374416a42ff5c190e9c0a06bba7f80b0e6.
Normal BP was built, independently reloaded/verified, then replayed in Editor and PIE (four poses each). The main project's stable DLLs were not replaced.

Implemented native input triangle provenance in the lower cage, resident GPU source adjacency exclusion, same-region adjacency/opposed-surface filtering, A1 support/source diagnostics, and a GPU shared-source regression. Legacy particles use invalid source IDs. Exclusion is scoped to an instance, not shared across actors. No source vertex welding or skin-weight changes.

The ankle-end blend experiment (candidate 5) FAILED: knee60 invalid cells increased from 1 to 5. It was reverted in candidate 6. Logs/assets remain locally for reproducibility.

Canonical cage validity is not sufficient. Actual bone-only mapping at deep knee flexion already has 498 inverted cells; final pose morphs reduce this to 399. Candidate 6 still has 399 (380 near the knee), knee60 still 1, seated still 1738. These failed poses never initialize contact. Do not call their screenshots successful squeezing.

Standing residual remains 0.6524 cm (max), concentrated near the gluteal midline. Source adjacency regression passes, but this is NOT proof that the standing residual was eliminated or that all of it is false contact. Candidate 5 was 0.6356 cm; candidate 4 was about 0.6765 cm.

Candidate 6's five native audit layers are exactly unchanged from candidate 4 for all four captured poses, including actual bone matrices (max difference zero). This demonstrates preservation of those static native outputs, not a new jiggle animation acceptance.

Final candidate 6: 16 explicit named automated test successes, including B1 FinalCorrective, glute/leg, breast, GPU contact, shared source exclusion, equivalent Euler limits. Engine startup Condition failed messages are retained; test completion is not inferred from process status. Editor physics backend is inactive by design; actual solver status is separately recorded from PIE.

Offline local/global positive-gradient experiments were not adopted. Fixed inner support still leaves 96 invalid cells after 100 iterations, with up to 3.78 cm displacement. Free inner support leaves 1 after 300, moving internal support 1.14 cm. Neither is a runtime acceptance result.

Full report, logs and captures: Saved/LowerBodyContact/KneeCompression/round-2/report.html and candidate-5/candidate-6. Recreate the report with Scripts/report_knee_compression_round2.py. Full bad-cell JSON digests and screenshot digests are in local-evidence-manifest.json; selected worst cells and four real PNGs are committed here. Audit script: Scripts/audit_contact_invalid_cells.py.

Next unresolved structural work: a valid material reference distinct from invalid animated targets, with anatomically consistent internal attachments. Do not increase iterations, relax inversion safety, or move surface targets away merely to manufacture a passing result.

Research reference: https://www.tinymonkey.org/projects/pixar/walle/fem/fem/ — author's account of soft constraints, rest-state retargeting and surface-to-volume diffusion; not evidence that the current project implements that method.

## Additional structural probes (not adopted)

2202 traceable near-boundary samples compare cage target to barycentrically sampled actual skinned source triangles. Deep-knee maximum discrepancy 0.322 cm, seated 0.351 cm. The neutral preskin plane tolerance is 0.01 cm; this sampling does not claim complete boundary coverage. Approximate attachment covariance compensation changes deep-knee invalid cells 399 -> 403, seated 1738 -> 1725, knee60 1 -> 1. It is not the main inversion remedy.

Independent canonical Stable Neo-Hookean material + current animated attachments was tested OFFLINE only. Articulated core pins were restricted to high-dominance shaft neighborhoods (2722 -> 1858 pins), with unchanged remaining attachment targets. Direct 500-iteration solve leaves 22 inverted cells and maximum target displacement 6.62 cm. Twenty incremental pose steps, 150 L-BFGS iterations each, leave 24 inverted cells, minimum J -0.572, volume ratio 0.99528, maximum displacement 5.34 cm. Neither solve converged or passed. No soft self-collision or rendered acceptance is included in these probes. They were not installed or represented as GPU results.

The initial continuation process was interrupted after step 5 because of host BLAS oversubscription; the saved complete result is a fresh run with OPENBLAS_NUM_THREADS=1 and OMP_NUM_THREADS=1. Its elapsed time is offline CPU time, not a game performance measure. Further work requires consistent joint attachment/volume geometry; these experiments do not prove a finished alternative architecture.
