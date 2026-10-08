# C2 body compression follow-up

This iteration addresses the rejected nipple-dominated contact result. It keeps
Chaos Flesh quasistatics and native GPU skeletal skinning. No Step 3 soft-soft or
two-way rigid coupling is introduced.

## Research translated into changes

- Naturalis Advanced Soft Physics separates mesh pressure, depth response and
  local distortion: https://everlastervr.github.io/docs/naturalis/v1_3/advanced_soft_physics/
  We do not copy its joint solver. It motivates checking neighboring surface
  expansion and indentation coverage instead of nipple displacement alone.
- Sheen/Larionov/Pai: https://elrnv.com/projects/volume-preserving-simulation-of-soft-tissue-with-skin/
  Volume, cell compression and skin response must work together. Our existing
  zonal volume and surface constraints remain; distributed interior samples
  replace the single-center star connectivity.
- VICTRE: https://breastcompress.readthedocs.io/en/latest/operation.html
  A volumetric body with fixed support and explicit compression surfaces is a
  useful comparison. This remains a game approximation, not its medical model.

## Implementation

Schema 3 is explicitly authored; historical class defaults remain schema 1.
The evidence-derived convex envelope is unchanged. A conforming inner layer
interpolates each boundary sample halfway toward the core, including skin/Morph
data. Each triangular frustum is split using globally ordered diagonals; the
inner layer closes at the core. Unlike the original star, contact loads pass
through a distributed intermediate layer. Positive cells, neutral volume
(relative tolerance 1e-6), binding reconstruction (0.001 cm) and Morph lengths
are checked. The attempted unconstrained Delaunay variant was rejected: cleaning
its degenerate cells changed the envelope volume by 5.4%. It is not shipped.

Only the near-root foundation receives animation position targets. The bulk
responds through elastic volume elements and the existing skin/volume constraints.
Nipple shape retention remains relative rather than a kinematic pin.

Debug contact defaults to a body-centered flat platen. Its stroke accounts for
the gap between initial nipple contact and body contact. Sphere and upper-offset
sphere are available independently. Stroke percentage describes indenter travel,
not measured anatomical strain. UE TBox PhiWithNormal uses its AABB; this test
platen is deliberately not advertised as rounded.

GPU validation excludes source-evidence nipple triangles and measures the area
and mean displacement of the remaining surface. At least 10% must move over
3 mm. This engineering check does not assert visual realism.

## Limitations

The volume is still a convex envelope proxy, not a fitted anatomical chest-wall
mesh. No calibrated epidermis model, tissue-specific medical modulus or active
muscle response is claimed. Flat-platen edges are sharp. Real UE images and numerical validation for this iteration are recorded below.

## Verified result — 2026-10-07

- Source HEAD: `2922d9fe16616cd8f182cdc49455aa8429e7be19`; changes remain
  uncommitted on `feature/chaos/breasts`.
- Final immutable output: `/Game/VamRuntime/R_2e2c36ae0ed20ba6065ff18c`.
  BP: `BP_VamCharacter`; configuration: `RC_Runtime`; profile:
  `DA_BreastContact`; GPU deformer: `DG_BreastContact`.
- Builder schema 3: 912 particles, 3608 positive tetrahedra. Imported envelope
  volume and zero-offset skin reconstruction passed. Save/reload/verification
  job `Saved/LegJiggleT1/PrimaryContactBodyFinal` is committed.
- Editor and Game build succeeded. GPU `Vam.Breast.ContactRuntime` and the
  separate native breathing/Jiggle run both passed on the final asset.
- The controlled GPU capture disables breathing, idle and Breast Jiggle, so
  its before/after samples share the same pose. Zero-pressure residual prints
  0.0000 cm; the test requires <0.05 cm. Native runtime functionality is not
  disabled by this test-only setup.
- Excluding source-evidence nipple triangles, measured area 208.2191 cm2;
  70.67% moves over 0.3 cm; area-weighted mean displacement 0.5695 cm;
  maximum triangle-average body displacement 1.4432 cm. This measures motion
  coverage, not indentation depth, displaced anatomical volume or realism.
- Left platen press: max skin delta 1.5399 cm, cage volume error ~0.014%,
  minimum local volume ratio 0.6349, zero inverted cells. Nipple relative
  distance RMS strain: uniform 0.038652, retained 0.020358.
- Off-nipple sphere, right press, release, translation/rotation, material
  preservation and restoration of native skinning pass. No-contact has zero
  active solvers. Native total world tick during press is ~27 ms on this host;
  contact solve is roughly 25 ms. This is a quality iteration with a significant
  active CPU cost, not a performance improvement over the previous cage.

### Contact corrections

The flat indenter extends farther than its stroke, preventing an elastic
iteration from exiting the opposite face of a thin slab. In addition to the
pre-collision compression barrier, a post-collision barrier moves compressed
cells in contact-tangent directions. This protects cells after membrane/contact
projection. The solver initializes from the current final pose; it does not
interpret initial pose mismatch as applied pressure.

### User controls and evidence

In VaM character debug -> Breast Jiggle -> Breast Chaos Contact:
`Platen - body`, `Sphere - body`, `Sphere - upper offset` choose a probe and apply
left-side pressure. Existing left/right 20% buttons use the selected probe;
release withdraws it. `Show press spheres` also shows the new platen wire box.
Use the NEW BP above in an ordinary level; old BPs retain their old profiles.

`Saved/BreastChaosC2/visual-body/compare.html` shows normal/pressed/released
real UE images with side and oblique views. `press-release.mp4` is 768x768,
30 fps, 180 frames / 6 seconds. These are genuine runtime captures, not generated
or edited deformation images. Visual acceptance belongs to the user.

### Local refinement question

Dynamic refinement must distinguish render triangles from simulated tetrahedra.
Adding only render triangles cannot create missing physical response. Epic states
that Flesh collision is vertex based and changed topology requires regenerated
bindings: https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart
The present build pre-refines the simulation cage and interpolates skin/Shape
responses. It does NOT dynamically subdivide the render SkeletalMesh at a contact
point. A future contact-aware refinement must budget collision nodes, transfer
state/volume consistently and preserve Morph/skin binding continuity. More
resolution everywhere would aggravate the active CPU cost above.

Cook: 715 packages, 0 errors / 0 warnings (`cook-body.log`). Installed DLLs were backed up and SHA256-verified; see `Saved/BreastChaosC2/installed-body.json`.


## 2026-10-08: contact surface normal refinement

The Flesh template computed corner angles but used equal face weights during tangent accumulation. Generated contact graphs now use atan2 corner-angle weights, retain duplicate-vertex normal accumulation / separate UV tangents, and guard degenerate face and UV calculations. Graph generation fails if the expected normal kernel is missing. This changes shading only: no render topology, cage topology, physical parameter, position kernel or skin weights change.

New immutable Blueprint: `/Game/VamRuntime/R_d44020d1d8d5f1bedfdecfe7/BP_VamCharacter`.
Previous comparison: `/Game/VamRuntime/R_2e2c36ae0ed20ba6065ff18c/BP_VamCharacter`.
Evidence: `Saved/BreastContactNormals/compare.html`, `visual-smooth/`, `runtime-body-visual.log`, `cook.log`, `installed.json`.

Validation: Editor build succeeded; asset committed and independently reloaded; GPU ContactRuntime passed; Windows cook 715 packages, zero errors/warnings. GPU vertices 25066, max contact delta 1.5399 cm; body surface mean displacement 0.5695 cm, affected fraction 0.7067, unchanged from previous reported measurements. These are aggregate comparisons, not a bitwise comparison of every vertex. Runtime C++ was unchanged, so no new Game C++ build was required. DLL installation hash checked, previous files backed up.

Actual same-camera captures show a limited shading improvement near the nipple; a ring-like contact crease remains. This is not a complete removal of faceting. Interpolated normals cannot change the real silhouette or resolve discontinuities in the deformation field. No visual acceptance claimed.

References:
- https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-rendering-paths-in-unreal-engine
- https://learn.microsoft.com/en-us/archive/msdn-magazine/2014/october/directx-factor-pixel-shaders-and-the-reflection-of-light
- https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-42-deformers


## 2026-10-08: render-only breast surface refinement (accepted candidate)

User explicitly approved locally increasing render triangles while retaining the physical cage. No source asset was overwritten.

### Diagnosis

Same-camera captures with dynamic shadows disabled show polygonal lower contours even before pressure. Angle-weighted normals alone cannot alter them. The diagnostic captures are in `Saved/BreastContactContinuity/visual-audit/`. The wireframe show-flag attempt produced a black frame and is not used as evidence.

### Implementation

- Added `VamBreastContactSurfaceRefinement.cpp`, automatically called after the existing contact cage build and before Deformer generation.
- Two bounded, adaptive curved-edge midpoint passes selected by existing continuous contact masks. Target edge length is 6% of the smaller calibrated breast radius, with a numerical floor. This is a bounded cubic Hermite edge construction using endpoint tangent planes, not a complete PN-triangle implementation or a global C1 guarantee.
- Selected edges are shared across adjacent triangles. Transition faces split as needed, avoiding T-junctions. UV/material copies retain separate UVs with source-identity matched curve normals. Original vertex positions, normals and indices remain intact; only new samples are appended.
- UVs and weights interpolate at edge midpoints; new influences are deterministically reduced to eight and normalized. Each Morph evaluates the same bounded edge operator. Native mesh construction validates Morph reconstruction, materials and influence invariants.
- Geometry bindings persist `RenderRefinementVersion` and `RenderRefinementParents`. Appended inputs explicitly use source INDEX_NONE with earlier-input parent provenance, rather than claiming an invented original source vertex. Existing rig/profile calibration remains unchanged; diagnostic region arrays extend through the same parent correspondence.
- Reuse the exact existing physical particles/tets. New render samples bind with nonnegative tetrahedral weights and a detail offset. The visible rest curvature remains in the native mesh.

### Precision and normal regressions caught during iteration

The first densified candidate showed micro-faceting when contact was enabled, even at zero pressure. Absolute positions interpolated with the Flesh interface's half-precision weights then subtracted from original positions introduced avoidable error. New profiles set `bResidualOnlySurface`; the producer and graph now interpolate the contact displacement itself. No absolute-position subtraction or rotated detail offset is used on this path. Old profiles default false and retain their original graph contract.

Recomputing geometric normals discarded authored smooth curvature. New residual graphs transport the authored normal using the inverse transpose of the local contact deformation gradient, and the tangent using the forward gradient; corner-angle accumulation and UV-separated tangents remain. At zero deformation this transport is the identity. This does not blend away pressure normals or blur rendered images.

### Verified candidate and evidence

- Blueprint: `/Game/VamRuntime/R_05f35415258049242c2accf1/BP_VamCharacter`.
- Mesh: `/Game/VamRuntime/R_05f35415258049242c2accf1/ContactSurface/SK_Body`.
- Contact profile: `/Game/VamRuntime/R_05f35415258049242c2accf1/DA_BreastContact`.
- Build receipt: `Saved/LegJiggleT1/PrimaryContactCurveNormals/runtime-report.json` (committed, independently reloaded).
- Input vertices 24,928 -> 33,699; final split render vertices 33,981; triangles 44,474 -> 62,016. Physical particles 912 and tetrahedra 3,608 unchanged. Public particle fields compare identically with the previous accepted profile; C++ construction verifies unchanged physical topology counts.
- Editor and Game builds passed. GPU/native `Vam.Breast.ContactRuntime` and `Vam.Breast.ContactRenderCurve` passed. Windows cook: 720 packages, 0 errors, 0 warnings.
- Added test-only CPU-native versus GPU zero-contact vertex comparison: maximum error 0.00004591 cm (<0.005 cm guard). This CPU reference is not used by ordinary runtime characters.
- Maximum GPU pressure displacement remains 1.5399 cm. Body-only measurement region now includes smaller boundary triangles, so its area/mean must not be compared as an identical sample set with the coarse mesh.
- Material/reset/release, unilateral press, off-nipple sphere, translation and rotation checks are included in ContactRuntime. Native run retains breathing/Jiggle inputs. No claim that every possible Shape/pose was covered.
- Actual images/video: `Saved/BreastContactContinuity/compare.html`, `visual-transport/press-release.mp4`. Failed intermediate render candidates remain recorded separately and are not the recommended output.
- One capture was launched before its runtime bundle was committed and the test's unchecked body cast failed; it was rerun after publication. Final tests above ran against the committed output.
- Installed DLLs hash-verified; backup and hashes in `Saved/BreastContactContinuity/installed.json`.

### Limits

Finite render tessellation still limits silhouettes. The physical cage and its piecewise deformation field remain finite resolution; this does not add new contact features, self-collision or two-way reactions. Additional render vertices cost GPU bandwidth and Morph storage even though solver topology is unchanged. No visual acceptance or performance gain is claimed.

References:
- Curved point-normal surface motivation: https://alex.vlachos.com/graphics/CurvedPNTriangles.pdf
- Deformation gradients and normal transport: https://developer.nvidia.com/gpugems/gpugems/part-vi-beyond-triangles/chapter-42-deformers
- Shadow/geometry distinction: https://dev.epicgames.com/documentation/unreal-engine/virtual-shadow-maps-in-unreal-engine


## 2026-10-08: C4 bulk/surface contact and visible rigid probe

User accepted the render contour but reported a plastic-film-like pressure patch, then requested a visible transparent rigid sphere instead of an invisible press.

### Diagnosis and changes

- The default test tool was a sharp rectangular platen. Its footprint must not be interpreted as a fingertip or a rounded hand. The debug default is now spherical; the platen remains explicitly labeled as a sharp-edge diagnostic.
- Native particle contacts alone can miss a sphere lying between physical surface vertices. Added closest-point, barycentrically distributed triangle contact inside the Chaos iteration, including a final contact projection after volume/edge correction. This introduces no new simulation degrees of freedom. The closest-point sampling is exact for a sphere against a flat triangle; it supplements native vertex contact for other simple shapes, not a complete generic continuous collision algorithm.
- First-touch placement now considers triangle interiors. A clipped polygon determines platen support; sphere support uses projected triangles and a bounded binary search. Spherical stroke starts at actual surface contact, without the platen's nipple-clearance addition.
- Profile schema 4 stores `LocalCompressionResistance=0.35`: a continuous cell compression response proportional to `(max(0,1-J))^2/max(0.2,J)`, with the existing emergency compression floor. It is an engineering projection law, not a fitted constitutive tissue model or a reproduction of a paper's full optimizer.
- The post-contact continuous correction is applied gently only in the final sweep; corrections below 0.01% of cell volume are skipped. Applying it in every sweep was rejected after reduced indentation and right-side inversion. Pre-contact-only correction was stable but had negligible effect on the affected cells.
- `SurfaceBending=0.05` uses Chaos's native rest-dihedral PBD bending constraints on neighboring physical boundary faces. A volume-aware backtracking step prevents the bending operator itself from inverting tetrahedra. A stronger 0.5 setting failed the world-probe case and was rejected. This is a curvature regularizer distinct from bulk elasticity and volume; it is not an anatomical skin modulus.
- Existing profiles load the new scalar fields as zero. Face contact is gated on schema 4. Each component owns its bending state. Physical cage topology, render refinement, skinning, Morph correspondence, Jiggle parameters and materials are retained.

### Evidence method

`Vam.Breast.ContactRuntime` now also creates an actual movable StaticMesh sphere with ordinary WorldDynamic simple collision, transparent rim/highlights, and no refraction. The sphere is mechanically driven (kinematic); it does not claim a simulated two-way rigid reaction. Hidden press sources are cleared during this sequence. Front and side captures cover approach, held pressure and withdrawal, plus a fixed-step video. GPU vertex distances measure penetration into the visible sphere. The 2 mm test threshold was not loosened when an intermediate candidate measured 3.15 mm.

The new `Vam.Breast.ContactBulk` test covers continuous compression, monotonicity, volume-unit scaling, legacy zero-resistance behavior, first touch inside a triangle whose vertices lie outside the probe footprint, and rigid-frame invariance. Existing zero-pressure GPU/native, materials, release, left/right, offset, and teleport checks remain. A transient Profile override is available only through the explicit automation trial flag; final evidence must use the serialized generated profile without it.

Comparison: `Saved/BreastContactBulk/compare.html`. Rejected trial logs are retained in the same directory. Final build/profile/test/cook/install results are recorded below when complete.

### Research and limits

The zonal-volume/local-compression distinction and separate epidermal response are informed by Sheen, Larionov and Pai, *Volume Preserving Simulation of Soft Tissue with Skin* (2021): https://arxiv.org/abs/2109.01170 . Large-deformation constitutive artifacts are also discussed in Smith, de Goes and Kim, *Stable Neo-Hookean Flesh Simulation*: https://graphics.pixar.com/library/StableElasticity/paper.pdf . SideFX's FEM compression example provides a practical solid-indenter comparison: https://www.sidefx.com/tutorials/fem-compression-simulation/ . We retain Chaos's existing corotated bulk solve; this change does not implement a new Neo-Hookean FEM backend.

Finite physical resolution can still produce localized creases. No self-soft collision, cross-character soft contact, two-way rigid reaction, anatomical validity or visual acceptance is claimed. Offline video timing is not a real-time performance benchmark.


### Final C4 verification and delivery

- Runtime: `/Game/VamRuntime/R_05393c71101a04b1d7f0ed32/BP_VamCharacter`; Profile: same root `/DA_BreastContact`.
- Receipt: `Saved/LegJiggleT1/PrimaryContactC4Skin/runtime-report.json`, committed and independently reloaded. The final runtime-only adjacent-tet safety fix was compiled after asset generation; no generated data was rewritten. Both final tests use this saved asset with the final binary, without trial overrides.
- Added adjacent-tetrahedron backtracking for barycentric contact corrections. This fixed an inversion during the native animation/Jiggle + rotated-teleport test. Bending-only backtracking did not cover contact-induced inversion.
- Editor and Game builds passed. Native and GPU ContactBulk / ContactRenderCurve / ContactRuntime all passed. Windows Cook: 720 packages, zero errors, zero warnings.
- Saved schema 4 / compression 0.35 / bending 0.05 audited through a separate UE process. Original physical particle fields compare identically to the prior accepted character. 912 particles / 3608 tets; 33,981 split render vertices / 62,016 triangles retained.
- Zero-contact GPU/native maximum error: 0.00004591 cm. Sharp-platen maximum GPU displacement: 1.5477 cm (previous 1.5399 cm).
- Actual world sphere held pressure: volume error about -0.16%, min tet ratio 0.3267, no inverted tets, maximum sampled render-vertex penetration 0.019203 cm (about 0.19 mm). This is not a continuous-surface collision guarantee.
- Evidence: `Saved/BreastContactBulk/compare.html`, `visual-transport/glass-{normal,pressed,released}.png` with side views, and `visual-transport/press-release.mp4` (768x768, 180 frames, 6 seconds, fixed-step offline capture).
- Installed the final DLLs, PDBs and module manifest after confirming no interactive UE process; SHA256 verification and backup path: `Saved/BreastContactBulk/installed.json`.
- Active CPU contact remains expensive (around 30–40 ms in these runs, some concurrent with other UE work). No performance improvement or real-time 60 FPS claim. Visible local creasing remains a limitation of the finite cage and skin model; no visual acceptance claimed.
