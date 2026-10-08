# Native tetrahedral meshing and volume-response exploration

Date: 2026-10-08. Baseline HEAD: `18bd300` (`feature/chaos/breasts`).

## Scope and provenance

This is an offline comparison, not an installed runtime replacement. No BP,
source character, committed profile, or runtime parameter was changed.
Editor build and `Vam.Breast.ContactMeshingComparison` succeeded in the isolated
HostProject. Source profile: `/Game/VamRuntime/R_05393c71101a04b1d7f0ed32/DA_BreastContact`.
Both sides of this single character were tested; this is not two-character validation.

The C++ automation calls UE 5.8's actual `FTetWild::ComputeTetMesh` and
`TIsosurfaceStuffing<double>` implementations, also used by Chaos Flesh's Dataflow
tetrahedral node. It exports geometry only. It does not run a new Chaos pressure solve.

Same closed input boundary for all native variants. TetWild: relative edge .15/.10,
relative tolerance .001, max iterations 40, other wrapper defaults. Iso: 6/9 cells
along the longest bounding-box dimension, signed-distance zero contour. The experiment
uses absolute winding magnitude for inside/outside orientation invariance. The engine
Dataflow implementation's `.5 + OffsetPercent` threshold is NOT used: .5 on a
distance-valued function would erode the original boundary. First failed attempts
and the final native log remain in `Saved/ContactMeshingComparison`.

## Native geometry results

Quality is mean ratio `12*(3*abs(V))^(2/3)/sum(edge_length_squared)`; regular tet = 1,
degenerate limit = 0. Threshold .1 is an experimental diagnostic, not a universal
physics pass criterion. Counts/timing are generation metrics, not runtime FPS.

Left side:

| Method | Particles | Tets | Quality P05 | Median | Fraction below .1 | Max node incidence | Initial volume cm3 |
|---|---:|---:|---:|---:|---:|---:|---:|
| Current layered |459|1816|.0498|.2278|13.11%|454|3423.94|
| Iso 6 |403|1392|.4190|.9075|0%|38|3288.54|
| Iso 9 |971|3945|.5539|.9524|0%|36|3368.79|
| TetWild .15 |670|2616|.5992|.8141|0%|48|3422.01|
| TetWild .10 |734|2898|.6177|.8425|0%|56|3421.64|

Right-side results agree in direction: layered P05 .0535 vs native .5371–.6252;
12.39% low-quality cells vs 0%; maximum node incidence 448 vs 36–58.
No degenerate tetrahedra in exported meshes after consistent orientation.
Current baseline is loaded, so its recorded generation time 0 means unmeasured.
Native Iso took .016–.041 sec/side; TetWild 6.6–7.1 sec/side on this run.

Iso 6 loses ~3.2–4.0% initial proxy volume, Iso 9 ~1.6–1.7%; TetWild loses ~.06–.07%.
These are meshing approximation errors BEFORE compression, not solver volume loss.
All volumes are collision proxy volumes, not anatomical/medical breast volumes.

The extreme central connectivity and poor cell quality support changing the layered
topology. They do not prove that a specific visible crease follows a specific edge;
that still needs a runtime deformation/edge overlay. TetWild .15 is the preferred
next integration candidate because of boundary fidelity with moderate count growth.
More nodes alone do not guarantee smoother contact or acceptable frame time.

## Independent material diagnostic (NOT Chaos)

`Scripts/compare_contact_meshing.py --compression` loads UE-exported meshes and
minimizes a dimensionless stable Neo-Hookean-style energy with SciPy L-BFGS-B:

`W = mu/2*(||F||^2-3) - mu*(J-1) + lambda/2*(J-1)^2`

with mu=1, lambda=2, plus an inversion penalty below J=.1. The volume-controlled
case adds `K*V0/2*(V/V0-1)^2`, K=1000. This is a finite penalty, not an exact volume
constraint and not a reproduction of the full Sheen/Larionov/Pai paper. It has no
skin layer, attachment, contact, friction, inertia, or material calibration.

Axial coordinates are prescribed at .96/.92/.88/.84/.80 of rest; lateral coordinates
are solved. This laboratory axis has no role in the production anatomical algorithm.
Analytic gradients are checked by a deterministic finite-difference directional test.
An earlier free-plane attempt allowed the asymmetric sample to rotate out of the
load; it was rejected (`rejected-free-rotation-compression.json`). It is not evidence
for pressure-induced bulging.

At 20% prescribed axial compression, all three tested meshes (layered, Iso6, TetWild15)
give approximately:

| Material variant | V/V0 | Lateral width ratio |
|---|---:|---:|
| Compressible | .8750 | 1.0458 |
| Zonal penalty | .9997505 | 1.1179 |

No final inversions. Every optimization stage's convergence/iteration status is
preserved in `compression.json`. The expected uniform solution independently gives
J=1-.25/(lambda+K) and transverse stretch=sqrt(J/.8), explaining these results.
The comparable equilibrium across meshes is expected for this homogeneous test;
it is not a localized indenter test. The layered mesh needs substantially more
optimization iterations. L-BFGS iterations must not be interpreted as Chaos timing.

## Research interpretation and next integration experiment

The requested behavior is nearly incompressible deformation: indentation displaces
volume into surrounding material. It is distinct from retaining a scalar total
volume while allowing local cells to collapse, or increasing rest inflation.

1. Preserve current rendered rest shape and transfer attachments/Morph responses to
   a transient TetWild profile; rebuild surface bindings and check zero deformation.
2. Compare the same actual sphere trajectory using native Chaos, same material and
   contact settings, with physical-cell and render-surface overlays. Measure local J,
   indentation, displacement outside the footprint, triangle-gradient changes,
   penetration, total volume, and frame cost. This step is NOT completed here.
3. If volume is retained but bulging is wrong, investigate coupled bulk/shear/skin
   response and attachment constraints. Do not compensate by global render scaling
   or increase inflation. Keep total volume and local compression metrics separate.

References accessed 2026-10-08:
- Epic native meshing overview: https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview
- Epic default properties (incompressibility vs inflation): https://dev.epicgames.com/documentation/unreal-engine/node-reference/Dataflow/SetFleshDefaultProperties
- Historia tutorial includes Vertex Incompressibility; it does not establish absence of volume preservation: https://historia.co.jp/archives/39713/
- Sheen, Larionov, Pai 2021: https://elrnv.com/projects/volume-preserving-simulation-of-soft-tissue-with-skin/
  Zonal constraints + cell compression + epidermis; cautions about locking when
  near-incompressibility is enforced through high Poisson ratio on coarse meshes.

Evidence: `Saved/ContactMeshingComparison/compare.html`, `quality.json`,
`compression.json`, ten native mesh JSONs, `run.log`, `build.log`, and plots.
No visual acceptance claimed. No installation, new character generation, cook, or
full runtime regression suite was performed in this scoped exploration.
