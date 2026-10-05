# G0 — Structural Glute Model

## Scope and contract

Branch `feature/jiggle/hip`, based on merged Breast V3 master `7f325187fd8af912c40e129913c6dfb218c80344`.
G0 is a deterministic pelvis/thigh pose function. It contains no glute solver clock, velocity,
inertial load, oscillator, free whole-glute angular mode, or G1 jiggle.
Breast V3 remains the production breast implementation.

## Anatomical basis and engineering interpretation

Gluteus maximus has pelvic/sacral origins and regional femoral/fascial insertions. Published
dissections disagree with a simplistic universal upper-to-ITB/lower-to-femur split.
The 2025 re-evaluation describes a plate-like superior tendon with femoral insertion and partial
ITB adhesion, plus a more complex inferior insertion. Surface skin weights cannot identify those
internal structures. G0 therefore estimates regional support from source evidence and geometry,
without claiming to reconstruct individual muscle anatomy or active contraction.

References:

- [Anetai et al., Structural re-evaluation of the human gluteus maximus, Scientific Reports 2025](https://pmc.ncbi.nlm.nih.gov/articles/PMC12214855/)
- [The tension band effect of the iliotibial tract, 1989](https://pubmed.ncbi.nlm.nih.gov/2740922/)

## Family mapping and region analysis

`Config/RigFamilies/VamFemale88.json: glute_structure` identifies pelvis, abdomen2 (superior),
LGlute/RGlute (source evidence), lThigh/rThigh and lShin/rShin (proximal femur geometry).
Missing mapping or evidence produces an explicit unsupported/build error. No preset/character
name or fixed world position appears in the algorithm.

The reference frame is attached to pelvis. Its superior direction follows pelvis-to-superior;
the transverse direction starts with the paired thighs; signed posterior direction is resolved
from original glute-weighted surface evidence. Axes remain right handed, with explicit side sign.
Source glute transforms are never runtime motion inputs. When original glute skin support is absent, paired source glute bind landmarks resolve the posterior sign and localize eligible pelvis/proximal-thigh surface evidence. Landmark kernel scale comes from femur length and glute-to-hip distance. This explicit fallback is recorded in provenance; it is not a character exception.

Each side combines original glute, pelvis and ipsilateral proximal-thigh weights with morph delta
energy actually concentrated on glute-supported vertices. Geometry supplies continuous posterior,
midline, superior and inferior gates based on weighted dimensions and femur length. Eight adjacency
diffusion passes use native triangles and weld only identical source vertex IDs. Donor evidence caps
diffusion. This limits leakage into back, abdomen, perineum, opposite side and distal thigh. The
transition is an estimate; region overlays still require human inspection on new shape extremes.

## Geometry and scaffold

Per side the appended hierarchy is:

```text
pelvis
└─ L_Glute_Anchor / R_Glute_Anchor
   ├─ ..._Core
   ├─ ..._Upper
   ├─ ..._Lower
   ├─ ..._Medial
   └─ ..._Lateral
```

All existing source and Breast bone indices/binds remain unchanged. With the current family,
source indices are 0–87, Breast helpers 88–99, G0 helpers 100–111. Anchor is kinematic; all five
children are structural regions, never independent simulated nodes.

Normalized spatial kernels partition the surface-to-pelvis-wall cone proxy:
`dV = projected triangle area * posterior depth / 3`.
Cone COM uses 3/4 posterior depth and the triangle's transverse/superior centroid. Regional volume,
mass-fraction candidates, lever arms, dimensions and inertia candidates are derived from this
partition. No fixed 40/15/20/10/15 fractions are used. The density candidate is explicitly separate
from support. These are effective engineering quantities, not medical volume or measured tissue
properties. Mesh closure/internal chest-wall anatomy is not assumed.

| Region | Structural responsibility |
|---|---|
| Core | Preserve main body and posterior projection; central future rest input |
| Upper | Pelvic support and superior contour |
| Lower | Proximal femur continuity and inferior transition |
| Medial | Pelvic/sacral tether near the midline |
| Lateral | Femoral/fascial influence and lateral contour |

Each node has a pelvis point projected onto its support plane and a proximal femur attachment
with a geometry-scaled posterior/lateral offset. The offset makes axial hip rotation observable.
Original donor weight evidence is averaged over each regional kernel. Source glute evidence is
split by geometric proximity, then combined with pelvis/thigh evidence and inverse distances.
The resulting normalized attachment shares are saved per region. Numerical floors retain both
attachments; shares are not authored per character. Regional trends are emergent estimates,
not enforced textbook percentages.

## Pose function and passive tension

At the final native skeletal pose hook, `Anchor = AnchorLocal * final pelvis`; femur input is
`final thigh relative to Anchor`. The imported/Shape rest femur transform is stored separately.
For each region, the fiber runs from its fixed pelvis point to its thigh-local attachment.

1. Compute current/reference fiber length ratio and direction change.
2. Passive tension is `gain * max(log(length ratio), 0)^2`. It is C1 at zero and represents passive
   stretch only. Different fibers respond differently to flexion, extension, abduction and rotation.
3. Modify and renormalize the two attachment strengths from that region's tension; pelvis support
   is retained. Save the resulting regional support baseline for a future G1 consumer.
4. Rotate toward the new fiber by the thigh share, with no free angular degree of freedom.
5. Apply smoothly bounded axial stretch with reciprocal square-root transverse scales.
   Their product is one. A smooth posterior barrier preserves a pelvis-dependent projection floor.
6. Output the regional helper transform relative to the final pelvis anchor.

Neutral pose returns imported rest exactly (floating-point tolerance). The positive/negative hip
poses change the fiber geometry; no direction is driven by a world-space lifting offset. Diagnostic
angles are a signed quaternion rotation-vector decomposition (flexion, abduction, external rotation),
not a clinical Euler-angle measurement.

The volume constraint applies to each regional affine scaffold. Blended skinning, the posterior
barrier and overlapping regions do **not** imply exact preservation of the entire skinned mesh
volume. G0 provides structural responsibilities and bounded support, not anatomical guarantees.

## Shape and native skinning

Builder precomputes morph responses for effective volume, COM, dimensions, regional rest/attachments,
regional volume and mass centers. Bone-center changes participate in the calibrated reference frame.
Runtime copies immutable profile data per component, evaluates current Shape, updates the rest femur
reference and support calibration, and writes stable helper indices. Shared USkeleton is never edited.
No motion history exists to misinterpret a Shape edit, teleport or frame-rate change as an impulse.

Skin weights use continuous region/root fade and normalized regional kernels. At most 70% of eligible
donor weight is transferred; pelvis/thigh/source-glute support remains. The existing Breast donor
compression utility is reused. Unrelated influences are untouched; final weights normalize to eight
or fewer influences. Native morph arrays, source correspondence and GPU skinning are retained.

The final pose hook reads source pelvis/thigh after animation, pose/IK/constraints and rigid blending.
It writes only G0 helper indices, then invokes the unchanged Breast runtime and native publication.
Rigid helper bodies/constraints are excluded. A future layer can read `GluteRest` and `GluteStates`;
G1 must compose residual motion on this structure rather than replace it.

## Asset transaction

Normal Resource Browser generation and Upgrade Runtime now run:

```text
native source → Breast V3 → G0 region/scaffold/weights/Shape responses
→ final rig/physics/materials/animation → RuntimeConfiguration → BP_VamCharacter
→ independent reload → publication verification
```

`DA_GluteStructure` is saved alongside `DA_BreastJiggle`. Extended final native assets are under
`<runtime root>/Glute/` so the Breast builder need not overwrite/rewrite its output. Both skeletons
are immutable; the final BP references the extended one. Upgrade adds the matching family mapping
and creates a new committed runtime root. Existing BPs/source assets are preserved.

## Observe in an ordinary Empty Level

1. Open the generated BP shown in the evidence, or generate/upgrade a character through Resource Browser.
2. Create **Empty Level**, drag `BP_VamCharacter` into it, and Play/Simulate.
3. Open the existing VaM character debug panel and select the runtime character. Expand
   **Glute Structure - G0** below Breast Jiggle. If clothes obscure the region, use the existing
   clothes visibility control; this changes visibility rather than deleting clothing assets.
4. `Enabled` toggles structural deformation. `Show Glute Region` shows imported region evidence;
   `Show Structural Bones` displays anchor-to-node lines; `Show Pelvis Attachments` and
   `Show Thigh Attachments` show the two attachment networks; `Show Pose Tension` labels passive tension.
5. `Neutral standing`/`Reset` clear G0 debug offsets on both thighs. `Hip flexion` requests 70°,
   `Hip extension` −20°, `Abduction` 30° mirrored, `External rotation` 30° mirrored.
   The normal joint constraints still apply. These commands set thigh debug offsets; they do not
   override an existing base animation or pose control. For isolated comparisons use the generated
   no-base-animation examples and neutral pose controls.
6. Diagnostics show signed hip angles, each side's effective volume/COM/AP-ML-SI dimensions,
   imported and current attachment shares, rest offsets, passive tension and support baseline.

The pose buttons provide hip poses, not a complete balanced bending animation. A user-authored bend
or thigh animation uses the same final-pose input path in any Level.

## Limits / excluded work

- Family semantics currently calibrated for VamFemale88 only; new families need mappings/evidence.
- Geometry/attachments are source-weight/surface estimates. Extreme Shape combinations use bounded
  first-order response approximations; new character distributions need human region/contour review.
- Regional determinant preservation is not a global mesh volume/contact solution.
- Fold references reserve medial infragluteal, mid-transition and lateral-fade semantics only.
- No active contraction inference, sitting compression, contact, collision, soft flesh, hand press,
  fold/wrinkle corrective, clothes deformation, or G1 jiggle is implemented.
- Existing clothing follows its original weights and can obscure the body.
- No visual naturalness, anatomical realism or visual acceptance is asserted.

## Evidence

See `Evidence/GluteStructureG0/` for the final build, two-character asset paths/calibration,
independent reload, automation and cook results. Full local logs are under `Saved/GluteG0/`.
