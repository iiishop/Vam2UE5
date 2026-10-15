# Knee compression investigation — incomplete contact acceptance

Baseline: 9028669. Four isolated candidates, no historical asset overwrite. Main project DLLs were not replaced by these experimental contact builds.

Implemented independent collision labels, proximal/continuous segment support, lower-body wake bounds, current pose corrective targets, exposed skin face filtering, and equivalent Euler limit handling without changing authored numeric limits.

Candidate 3: 15/15 joint regression tests passed. The initial EquivalentEulerLimits invocation found no matching test (UBT source discovery cache); it must not be counted as a pass. A forced -NoUBTMakefiles discovery/rebuild compiled VamPoseLimitTests.cpp; the subsequent actual test completion is Success (limit-results.json). Normal BP Editor and PIE were actually run. This is NOT contact acceptance: flexed poses still have inverted target tetrahedra and the solver correctly refuses initialization.

60-degree request, left/right nonpositive cells: baseline 2728/2790; proximal support 39/37; continuous support 8/8; pose-corrective target 0/1 (remaining cell at ankle). Limited Knee120 remains 72/78 in candidate 3. Suspended seat remains 807/931. Standing soft-only still exhibits 0.6765 cm maximum residual, requiring contact false-positive investigation.

Candidate 1 native contact-off output exactly equals the previous stable output across four poses and five layers (max 0 cm). This is not a new deep-flexion proof.

Full logs, source-identified A1 exports, actual angle/state JSON and real UE screenshots: Saved/LowerBodyContact/KneeCompression. Reproduce report with Scripts/report_knee_compression.py. Do not present failed solver screenshots as compression success.

Final candidate 4 identity: 7a90bf5720cd4399fb3a8177bc752e9a2a587aaf0acaaa589e7b6c2d83653e6a. Independent build/reload/verify committed. 15 existing regressions passed for this identity, plus the separately rerun new limit test passed. 24 UE captures retained across experiments. Editor and PIE Knee120 geometric bends agree: left 118.644577 / right 118.618091 degrees, with original numeric limits unchanged. Candidate 4 flexed contact still FAILS: Knee60 cells 0/1; Knee120 at this deeper pose cells 186/213; seat 807/931. These Knee120 counts are not the same pose as candidate 3's ~92-degree capture. Editor contact backend is inactive; PIE captures separately record actual solver state.

Engine startup logs contain pre-test Condition failed messages. Logs are retained; passing claims rely on explicit named Test Completed records, not exit status. The initial missing test was publicly corrected and is not counted as a pass.
