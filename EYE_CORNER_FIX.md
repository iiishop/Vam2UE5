# Eye corner topology correction

Final V20 Character, Assembly, measured tradeoffs and evidence paths are recorded
in [EYE_CORNER_RESULT.md](EYE_CORNER_RESULT.md). The following retains the
diagnosis and intermediate experiments; only the final official receipts define
delivery status.

This experiment addresses eyelid correspondence collapse and nonadjacent skin
triangle crossings. It does not certify visual fidelity or eliminate every
kind of eye contact. Native assets and the previous V9 Character are retained.

## Diagnosis

V9 merged the official upper/lower eyelid vertex sets and independently found
the nearest point on a source annulus. The official union is actually a single
60-vertex cycle in the original MetaHuman quad topology for each eye. Independent
projection lost this order. The surface objective also acted on these boundary
vertices, competing with the boundary correspondence.

The actual V9 post-rig skin has 13 distinct nonadjacent triangle pairs crossing
within the three-ring eyelid neighborhood (26 directed pairs). The original
initial-fit head has zero. This is evidence of a residual-fitting regression,
not merely a material/shading explanation. Auxiliary eye sections also intersect
skin; these counts alone do not distinguish intended socket overlap from an
unwanted visible intersection.

## Generic correction

* Recover the target cycle from official quad edges; reject ambiguous or
  disconnected cycles instead of guessing an order from vertex IDs or position.
* Fit a cyclic, orientation-tested source arc parameterization. Positive arc
  increments retain one full winding; bound increments to 0.25–4 times the
  initial target arc fractions to prevent independent nearest-point collapse.
* Exclude the constrained eye rim from the ordinary surface attraction term.
* During every solver step, detect proper transverse intersections between the
  eye skin neighborhood and nonadjacent skin triangles. Backtrack implicated
  vertices alongside the existing quad/edge guards.
* Include the same intersection check in candidate eligibility and in scoring
  the actual official Template/AutoRig exports. Source correspondences remain
  unverified; no calibration truth is invented.

V10 (ordered curves alone) still produced skin intersections and was not sent
to official generation. V11 adds the local intersection guard, but official
Template import reintroduced one crossing pair. Its Character remains Draft.
V12 adds a clearance barrier: retain at least 10% of initial triangle separation,
capped at 5% of median local edge length. These are relative mesh quantities.

V13 separates source exterior skin from socket interiors by cutting the
provisional closed eyelid rims in the connectivity graph. On the earlier
initial mesh, 139 of 360 exterior eye-neighborhood vertices preferred the
source inner wall under a normal-filtered nearest-surface comparison. The new
solver queries only the corresponding component. V14 additionally uses the
source exterior tangent field to reduce inherited crease bias.

V15 expands support from three edge rings to a geodesic radius derived from
the eyelid perimeter. This correctly rejects the old initial head: it already
contains 49 distinct crossing pairs in the wider eye region. Thus the earlier
zero-initial-crossing statement applies only to the smaller three-ring check.

An offline canonical-rest differential patch repair was investigated in
`EyeRestRepair`; it is not the selected production input. The V16 experiment
instead starts from the saved earlier official conform checkpoint before the
legacy surface transfer (`face_autocalibrated_Face/component.json`), which has
zero crossings over the wider eye region. Its MH-only rigid pose conversion
residual is approximately 1.3e-12 cm. No source-face registration is introduced.
The full source geometry still comes from the same locked neutral p0.

All profiles use the same constraints. Names and file paths identify inputs
and outputs only; there is no identity-dependent branch in the fitting code.

## Evidence and scope

Latest experiment: `Saved/MetaHuman/FaceFidelity/EyeCornerV20`.

V17 adds a conservative distance-bound acceleration to the same V16 guard.
The Conservative candidate was independently recomputed with both versions:
the maximum vertex difference is exactly zero. V16 was cancelled after retaining
its completed evidence; it was not sent to official generation.

V17 passed official Template geometry checks and completed AutoRig, but delivery
was held after the closeups exposed new upper-lid corrugation. It has no promoted
Assembly from this repair. V18/V19 are retained offline experiments. V20 uses
the same point/normal interpolation on the rim as on the source surface, keeps
the original reference normal field when restricting a surface to one ownership
region, and scales local displacement regularization by the squared target/source
rim sampling ratio (9 for this input). Cutting correspondence regions must not
silently change the reference surface at the cut.

The V20 run's exact geometry implementation is retained in `AlgorithmSnapshot`.
A subsequent hardening skips a failed cyclic orientation only if the other
orientation converges, instead of aborting both. Both actual eye correspondences
were recomputed after this change: segment identities and fractions are exactly
unchanged (`orientation-fallback-hardening-replay.json`). Cache hash checks are
not bypassed; a changed implementation still requires a new solver output or
the saved implementation snapshot to reproduce an older run.

Over the V17 shared geodesic ROI, the actual old V9 rig has **303 distinct
crossing triangle pairs** (606 directed pairs), versus the earlier 13-pair count
from the much smaller three-ring ROI. These scopes must not be mixed.

New saved-request runs through `vam_face_pipeline.py` enable ordered eyelids,
topology ownership, clearance, tangent refinement and the geodesic guard by
default, together with consistent curved rims and sampling regularization.
Existing cached jobs retain their locked settings. The supplied initial
official head must pass the wider eye preflight: an already intersecting input
is rejected with `InitialEyeSkinSelfIntersection`, not silently repaired or
passed through. Use a saved earlier verified official conform checkpoint and
a new output directory to retry. This change does not wire a new UI button into
the original importer.

The intersection diagnostic excludes adjacent triangles sharing vertices and
does not certify coplanar contact, tangency, continuous collision-free motion,
animation, or final material/depth rendering. It is a discrete neutral geometry
check. The auxiliary surfaces are retained and regenerated by official APIs;
the code does not delete them or modify the final assembled SkeletalMesh.

Source annulus identification and canthus semantics still need topology-family
calibration. Ordered correspondence preserves a curve but cannot by itself
prove that the selected source curve is anatomically homologous. Nose fitting
is not specifically modified in this iteration.

## Main files

* `Scripts/vam_face_eye_correspondence.py`: topology ordering and constrained arc fit.
* `Scripts/vam_face_eye_intersections.py`: neutral eye section crossing diagnostics.
* `Scripts/vam_face_eye_regions.py`: topology ownership and geodesic support.
* `Scripts/vam_face_adaptive.py`: ordered boundary integration and local guard.
* `Scripts/vam_face_adaptive_cli.py`, `vam_face_adaptive_verify.py`: candidate and official gates.
* `Scripts/vam_face_eye_views.py`: identical-camera source/old/new skin closeups.
* `Scripts/test_face_eye_correspondence.py`: ordering, collapse, invariance, crossing regression tests.

Delivery status is recorded in the experiment task and official receipts; this
document alone does not assert that a Character or Assembly was completed.
