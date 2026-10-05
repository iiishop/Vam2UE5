# Breast calibration v2 — research and model decisions

Date: 2026-09-28. Starting HEAD: `1c07fd5cb30f785dc231fccdfcebd37891246504`.

## Primary references

- [Naturalis 1.3: Physics Parameters](https://everlastervr.github.io/docs/naturalis/v1_3/physics_parameters/) distinguishes joint mass, COM, rotational inertia, rotational spring/damper, directional translation springs and joint limits. Its separate soft-physics and collider groups operate at a different spatial scale. This project borrows the separation of concepts, not its formulas, UI, collision system or proprietary implementation.
- [TittyMagic 5.2](https://everlastervr.github.io/docs/tittymagic/v5_2/) describes automatic mass estimation from approximate volume. [Calibration](https://everlastervr.github.io/docs/tittymagic/v5_2/about_calibration/) explains re-establishing a neutral state when physics changes. Here imported appearance remains the neutral gravity baseline; Shape recalibration is analytic and per instance, without freezing the scene or changing shared assets.
- [Naturalis 1.3](https://everlastervr.github.io/docs/naturalis/v1_3/) describes automatic size-dependent physics. The transferable idea is to derive a baseline from the character and leave fewer independent controls. Its Softness/Quickness macros are not reproduced.
- [NIST/ICRU composition table](https://physics.nist.gov/PhysRefData/XrayMassCoef/tab2.html) lists breast tissue density as 1.020 g/cm³ and adipose tissue as 0.950 g/cm³. We use **0.00102 kg/cm³** as a fixed versioned effective baseline. Geometry supplies no tissue-composition measurement. This choice establishes a useful mass scale; it does not make the surface-derived volume or the resulting dynamics an anatomical measurement.
- [Box2D simulation documentation](https://box2d.org/documentation/md_simulation.html) separates natural frequency from damping ratio and displacement limits. [PhysX implicit joint drives](https://nvidia-omniverse.github.io/PhysX/physx/5.6.1/_downloads/6acf3afb8f69452757e0e766b5a22978/implicitDrives.pdf) explains implicit spring/damper integration. Our solver remains project code and does not invoke Box2D or PhysX joint drives.

## Problems found in v1

1. `omega² = referenceMass / mass * (2*pi*f)² / Softness`. Density and Softness largely adjust the same small-signal response while leaving travel and coupling inaccessible.
2. Frequencies and semantic mass fractions are fixed for every character; measured support area and depth do not establish elastic coefficients.
3. All-to-all equal pair springs suppress differential semantic motion. Explicit coupling limits useful stiffness.
4. `cross(rest, displacement)` has no independent angular momentum.
5. `8 - sourceInfluences` can entirely suppress helper weights at the vertices most in need of them.

## Implemented approximation

- Keep the existing source-supported topology region and mirrored anatomical frame. Integrate effective cones from the chest support plane. Normalize five semantic kernels per cone; accumulate each partition's volume and mass center. No fixed mass-fraction table.
- Measure AP/ML/SI weighted extents and a root-biased extent. Root area remains the projected regional support proxy, not an independently reconstructed anatomical chest-wall boundary.
- Use `k = E_eff * area_share / effective_length`, with COM-lever, root-width and semantic attachment factors. Convert cm geometry consistently (`Pa * cm²/cm * .01 = kg/s²`). The **1200 Pa effective network modulus** is an engineering calibration coefficient, not a claimed laboratory measurement of tissue elasticity. Runtime stores stiffness; frequency is derived from mass and stiffness for diagnostics.
- Sum node parallel-axis moments plus finite local-volume spherical moments into a full symmetric tensor about the calibrated COM. This is a five-region quadrature approximation of mass distribution, not a volumetric tetrahedral FEM tensor.
- Support changes anchor translational and rotational stiffness; Damping changes ratios; Mobility changes translational and angular soft/hard extents; Internal Coupling changes graph edges only. Advanced Mass Scale changes mass and inertia only. Damping coefficients follow `c=2*zeta*sqrt(m*k)` so a fixed damping **ratio** remains fixed when mass/support changes.
- Implicit 15-variable nodal elastic/damping/Coriolis solve, with a sparse eight-edge graph. Core-to-periphery links are stronger than the four neighboring rim links. Edge stiffness depends on region volume and center distance.
- Independent three-axis angular displacement/velocity solve with full inertia, rotational support/damping, angular acceleration and reference-frame gyroscopic terms. The angular load is split from nodal forces; mass-orthogonal residual nodes avoid duplicating rigid rotation. Small-angle modal approximation uses an inertia tensor fixed in the chest frame.
- Increasing damping reduces underdamped ringing; beyond critical damping, increasing it can slow return to equilibrium. This physical distinction is documented rather than hidden behind a macro.
- AP travel uses depth, ML uses width/root width, SI uses vertical extent/lower hanging distance. Chestward AP travel is smaller and enters the nonlinear transition earlier. Limits strengthen continuously before a counted emergency projection.
- Donor-only compression frees helper slots while protecting all unrelated influences. A fully saturated vertex with seven non-donors and one substantial donor has no mathematically exact way to keep both that donor and an extra helper within eight slots. Such cases are counted explicitly, not resolved by stealing unrelated weights. Standard eight-influence cases with multiple eligible donors are covered by engineering tests.

## Boundaries

No contact, continuous tissue incompressibility, breast/cloth collision, skin wrinkle, or anatomical validation. The independent angular mode is a linearized COM mode, not a general finite-rotation rigid body with a rotating inertia tensor. Residual nodal and angular modes approximate coupled tissue motion. Shape uses precomputed local responses, then repeats calibration; extreme combined morphs can exceed the accuracy of those responses. Clothes still follow source skeleton weights. Numerical tests do not establish appearance quality.
