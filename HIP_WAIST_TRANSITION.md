# Hip upper-waist transition refinement

The reported jump fold is at the upper glute/waist transition. A screenshot alone cannot establish whether skin compression, corrective deformation, or shading is the final cause. Source inspection identified a relevant amplification issue: all five helper offsets, including the upper attachment, received the same 3x gain.

Added per-instance `GluteWaistTether` (0..1, default 0.85). Let u be the clamped projection of each regional rest position onto Core→Upper, normalized by that span length. Output gain = Amplitude × (1 - WaistTether × smoothstep(u)). The Core retains its requested gain; the Upper retains 15% of it at the default. Other regions vary continuously with their source geometry. Solver state, structural rest transforms, region membership, skin weights and source skeleton remain unchanged. Debug output positions and velocity vectors use the same regional gain.

Panel: Glute Jiggle - G1 → Waist Tether · 腰部衔接. Set 0 for the prior uniform amplification, default 0.85 for the upper-attachment fade. This is an output-level mitigation, not a per-triangle strain guarantee or a claim of visual acceptance. Existing latest BP works without regeneration. Compare the same jump and view with tether 0 and 0.85.

The Hip branch was fast-forwarded to b68ade5 before this change to preserve the completed Breast amplitude and Leg ring-down fixes. No automatic dynamics tests or visual acceptance were run for this change.

## Superior region expansion

Family policy `glute_structure.superior_transition`: extent_sigma=3.5 (previous 2.5), fade_sigma=1.5 (previous 1), evidence_sigma=3 (previous 2 for the upper half). Sigma is measured from original source-weight evidence in the signed pelvis frame, not fixed world centimeters. This moves both the fade start (1.5→2 sigma) and zero boundary (2.5→3.5 sigma) upward while broadening the transition. The source evidence envelope is broadened only above its mean, so the upper tail is not suppressed before the new gate. Posterior/side gates, pelvis/thigh donor eligibility, maximum donor fraction and inferior gate remain unchanged.

This is a body-surface glute coverage approximation; the source has no internal muscle segmentation and does not establish coverage of the entire anatomical gluteus maximus. Profile provenance records the policy, and skin identity includes family JSON. Builder algorithm is `glute-structure-g05-surface-v4-upper`. Upgrade refreshes this family policy and builds a new immutable output. A new generated BP is required because mesh weights change. Waist Tether remains available for upper support.

Delivery: Editor compiled and DLL installation hashes matched. New BP `/Game/VamRuntime/R_a3e162d37e0419cbf724624b/BP_VamCharacter` completed build, independent reload and publication (all exit 0). Read-only profile inspection shows region-weight >0.01 vertex counts L 534→573, R 559→602; at >0.1, L 291→308 and R 292→316. These counts are coverage diagnostics, not an anatomical or visual acceptance result. Reports: `Saved/LegJiggleT1/PrimaryUpperGlute/`. The previous BP remains available for comparison.
