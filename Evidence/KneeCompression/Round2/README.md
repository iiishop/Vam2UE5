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
