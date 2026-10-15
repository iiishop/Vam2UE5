# Knee source-operator repair — 2026-10-12

Implementation: `49f60e4` (pushed to `fix/allbody`).

## Root cause and scope

The source contract's fixed LBS approximation already loses the nonlinear
DAZSkinV2 knee response, before Glute/Leg helpers or Jiggle. Its fitting angles
are ±15/±30 degrees and validation is ±22 degrees. Large flexion exhibits
centimeter-scale collapse against the retained original VaM raw surface.

`vam_knee_source.py` recovers the actual imported shin axis weights and bulge
data. `VamKneeCorrectiveBuilder.cpp` reconstructs the isolated source-X response
(UE Y), subtracts final quantized LBS, and bakes inverse-skinned Morph targets
after all weight/topology edits. `VamKneeCorrective.cpp` drives the targets from
actual local knee twist relative to the current shape reference. The new data
participates in the existing final corrective identity seal and A1 stage 03.

This restores an evidenced part of the source skin operator, not a complete
anatomical knee or all-axis TriAx implementation. The target range is -15..150
degrees at 15-degree intervals. Existing triangle safety attenuation remains;
deep-flexion posterior residuals are not hidden or declared solved.

## Measured source comparison

24 core-on/off replays use identical captured VaM matrices and request IDs.
Source IDs, not nearest-neighbor matching, establish correspondence. The
pre-corrective stages are unchanged. Example left-knee core-off RMS (cm):

| Pose | Region | Before | After |
|---|---|---:|---:|
| Actual 90 | Anterior | 1.323 | 0.115 |
| Actual 90 | Posterior | 2.368 | 0.784 |
| Actual 120 | Anterior | 1.948 | 0.264 |
| Actual 120 | Posterior | 3.404 | 1.760 |

All bilateral metrics, worst source IDs and maximum errors are in the CSV.
The actual 120-degree geometry replay is separate from production screenshot
requests, which retain the existing joint limits. It must not be mislabeled as
a rendered unrestricted 120-degree production pose.

## Actual runtime validation

- C++ compiled; new independent asset build/reload/verify succeeded.
- 14 existing B1/Glute/Leg/Breast/weight/normal tests passed.
- `Vam.Knee.SourceOperatorDriver` passed.
- 21 normal-BP poses in both Editor and PIE in HostProject.
- Four additional poses in both Editor and PIE in installed SmartNPC:
  knee45, knee75, knee60 with Jiggle, seated with Jiggle.
- All 50 captures have finite surfaces and zero triangle-normal reversals
  relative to their pre-corrective posed surface. This is not a full
  self-intersection test.
- Main PIE knee60 Jiggle maximum surface displacements: L/R thigh
  0.759/0.865 cm, L/R calf 0.767/0.767 cm. Seated: thigh 1.037/1.031 cm,
  calf 0.106/0.106 cm. Existing dynamic muscle helpers remain active.
- Existing Contact rest-volume failures are separate; Contact is disabled in
  these pose tests. No new contact acceptance is claimed.

## Identity and reproduction

Identity: `645060862ddee7d2a4a6c45894480d29028411daadf3a6eca6ccbb8b594ffbd7`

Configuration:
`/Game/VamKneeStructure/Runtime/R_645060862ddee7d2a4a6c458/RC_Runtime`

Normal generated blueprint:
`/Game/VamKneeStructure/Runtime/R_645060862ddee7d2a4a6c458/BP_VamCharacter`

Local complete evidence: `Saved/LowerBodyContact/KneeStructure/report.html`.
This includes actual UE images, source-surface sections explicitly marked as
geometry plots, logs, per-stage exports, identity and installation hashes.

Reusable tools: `audit_knee_structure.py`, `audit_knee_runtime.py`,
`plot_knee_comparison.py`, `report_knee_structure.py`, and the existing
`run_s1s2_regression.py` / `audit_s1s2_regression.py` harnesses.

No medical claim is made: load-dependent tibiofemoral/patellofemoral kinematics,
patellar tracking, active muscle shape and thigh/calf soft contact need separate
evidence and implementation. Do not tune Jiggle stiffness to conceal the
remaining static posterior-knee residual.
