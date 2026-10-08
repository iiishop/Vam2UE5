# Skin / nipple contact investigation — 2026-10-09

Baseline `8563d98`. User reports CPU and GPU both show membrane-like local collapse. CPU is a comparison, not anatomical ground truth.

## Research and limits

- Chen et al. (2024), [multi-component breast FE model](https://d-nb.info/1338023845/34): internal tissues are volumetric elements and skin is a separate triangular shell, coupled through common interfaces. This is a running study with one participant, not a validated nipple indentation law. It motivates separating bulk and skin responsibilities, not copying its numerical constants into this character.
- [Mechanical response of human female breast skin under uniaxial stretching](https://pmc.ncbi.nlm.nih.gov/articles/PMC5582008/): experimental nonlinear skin response; supports strain-dependent skin behavior rather than a universal constant spring. Full-page access was blocked during this run; indexed primary text was available. No measured nipple stiffness is inferred.
- [SideFX Vellum softbody documentation](https://www.sidefx.com/docs/houdini/vellum/softbody.html): distinguishes global balloon volume from local tetrahedral volume and warns about irregular proxy element sizes. Conserving total volume cannot alone preserve local tissue shape.
- [PositionBasedDynamics authors' implementation](https://github.com/InteractiveComputerGraphics/PositionBasedDynamics/blob/master/PositionBasedDynamics/PositionBasedDynamics.h): separate bending and shape-matching constraints. Reference for architectural separation; no external implementation copied.
- Macklin & Muller, [constraint-based stable Neo-Hookean materials](https://matthias-research.github.io/pages/publications/neohookean.pdf): volumetric and deviatoric energy are separate. The present prototype is NOT an implementation or calibration of that material law.
- [Open breast FE pipeline](https://github.com/sioux-technologies/EWS-FEM-pipeline): anatomically separate components, including a modeled nipple/duct. Geometry and internal connectivity matter in addition to a surface stiffness multiplier.

No retrieved source establishes a universal nipple modulus or justifies a rigid nipple. Engineering shape retention must allow translation and tilt, and must be labeled separately from measured tissue properties.

## Observed code gaps

GPU had surface edge length bounds but no bending/continuity term, and omitted the CPU's nonlocal nipple distance graph. Local tetrahedral compression could remain large despite tiny total-volume error. Particle-only collision cannot constrain embedded render vertices between particles. These are model/discretization issues, not evidence that GPU hardware causes the artifact; CPU can share the same failures.

## Experiments (raw evidence: Saved/ContactSkinResearch)

1. `crosslinks`: nipple nonlocal distances plus opposite-vertex cross links. Rejected the cross links: fold appearance remained or worsened. No adoption of that bending surrogate.
2. `differential`: retained semantic nipple graph; Jacobi smoothing of displacement relative to animated rest (not smoothing imported positions). Nipple RMS strain 0.01227, edge stretch 0.942–1.227, volume +0.22% in captured pose, but visible vertex penetration 5.56 mm. Not sufficient by itself.
3. `surface-contact`: add barycentric embedded-surface collision constraints, distribute correction back to cage particles using inverse-mass and squared binding weights, gather without races. This is solver feedback, not a post-render projection. Rejected: inverted cells triggered CPU fallback. A CPU fallback frame is not a GPU success.

The differential term is an engineering regularizer, not a constitutive skin shell: it penalizes local residual rotation as well as curvature, and strength depends on iteration schedule. Nipple membership comes from existing source support (top 32 supported movable particles each side); no character ID or world-space feature coordinates.

Original 60 Hz capture trajectory, fixed rest/Jiggle disabled to isolate contact, actual translucent StaticMesh sphere. Capture tests are engineering checks only; no visual acceptance. CPU solver code is unchanged by these experiments.

## Additional controlled experiments

| Capture | Outcome | Decision |
|---|---|---|
| surface-limited / surface-sweep | No measured penetration, but surface wrapped around front of sphere; local max J about 6.2 | Rejected despite automation success |
| local-volume | Local volume band 0.8–1.2 caused inversion and CPU fallback | Rejected |
| directed-contact | Directional embedded contact caused inversion and CPU fallback | Rejected |
| cpu-shell | Existing native bending at 0.05; penetration 1.67 mm; surface creasing still present | CPU control, not anatomical ground truth |
| skin-normal | Shortest-arc normal transport did not visibly resolve artifact | Reverted |
| dihedral | Rest-dihedral bending; nipple RMS 0.07276; penetration 5.70 mm; local surface stretch up to 2.416 | Reverted |
| nipple-graph | Nonlocal source-supported feature distances alone; RMS 0.06598; penetration 5.56 mm | Reverted |
| continuum-strain | Frobenius strain constraint plus separate volume; RMS 0.09619; penetration 8.00 mm; stretch up to 5.095 | Reverted |

The lower penetration reported by failed/fallback experiments is not used to rank GPU quality. No test threshold was relaxed to make a candidate pass. Tests measure engineering constraints, not visual naturalness.

Further primary references:
- [Axelsson et al. 2022, mechanical imaging FE model](https://pmc.ncbi.nlm.nih.gov/articles/PMC9125329/): models constituent tissues and contact; validates pressure quantities, not an interactive nipple indentation appearance. Tissue distinctions motivate heterogeneous modeling but its constants are not copied as universal values.
- [breastCompress author documentation](https://breastcompress.readthedocs.io/en/latest/operation.html): nonlinear solid mechanics/contact and remeshing on convergence failure. This is an offline reference, not evidence of real-time suitability.

Captures use the same 20% glass sphere trajectory, camera and assets. Six-second animations are fixed-step captures and are not FPS measurements.

## Delivery status

No candidate met the combined deformation/contact requirements. Production solver, deformer and shader source restored to HEAD. Installed plugin DLLs were not replaced. Retained research, reproducible capture harness, logs, images and failed-experiment patches. Experimental GPUContact_Skin_20261009 assets are isolated and not promoted to a BP. No user character assets were overwritten. This task has NOT completed the requested pressure-quality fix.

The next implementation needs a coupled local constitutive/contact formulation with a consistent skin surface. A measured nipple constitutive law remains unavailable; a bounded three-dimensional feature shape model must be explicitly labeled an engineering approximation. Neither global volume error nor a single no-penetration check is enough to accept that model.
