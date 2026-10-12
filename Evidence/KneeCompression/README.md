# Knee compression investigation — incomplete contact acceptance

Baseline: 9028669. Four isolated candidates, no historical asset overwrite. Main project DLLs were not replaced by these experimental contact builds.

Implemented independent collision labels, proximal/continuous segment support, lower-body wake bounds, current pose corrective targets, exposed skin face filtering, and equivalent Euler limit handling without changing authored numeric limits.

Candidate 3: 15/15 joint regression tests passed. EquivalentEulerLimits passed separately after adding the limit fix. Normal BP Editor and PIE were actually run. This is NOT contact acceptance: flexed poses still have inverted target tetrahedra and the solver correctly refuses initialization.

60-degree request, left/right nonpositive cells: baseline 2728/2790; proximal support 39/37; continuous support 8/8; pose-corrective target 0/1 (remaining cell at ankle). Limited Knee120 remains 72/78 in candidate 3. Suspended seat remains 807/931. Standing soft-only still exhibits 0.6765 cm maximum residual, requiring contact false-positive investigation.

Candidate 1 native contact-off output exactly equals the previous stable output across four poses and five layers (max 0 cm). This is not a new deep-flexion proof.

Full logs, source-identified A1 exports, actual angle/state JSON and real UE screenshots: Saved/LowerBodyContact/KneeCompression. Reproduce report with Scripts/report_knee_compression.py. Do not present failed solver screenshots as compression success.
