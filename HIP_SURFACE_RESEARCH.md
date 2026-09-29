# Hip surface transfer and bilateral calibration research

## Sources consulted (2026-09-29)

1. FSMP author guide: https://github.com/DaymareOn/FSMP-Validator/wiki/25-%E2%80%90-Rigging-for-physics and https://github.com/DaymareOn/FSMP-Validator/wiki/20-%E2%80%90-4-bones-limitation-and-weight-painting . Kinematic anchors, simulated movers and actual skin weighting form one system. Skyrim's influence budget is not UE's budget; preserve our eight-influence limit. No FSMP source code is copied.
2. FSMP spring guide: https://github.com/DaymareOn/FSMP-Validator/wiki/33-%E2%80%90-Springs-stiffness-and-damping . Frequency depends on stiffness/mass; distinguish constraint damping from global velocity bleed. Our own implicit solver remains in use.
3. Naturalis BootyMagic/TittyMagic: https://everlastervr.github.io/docs/naturalis/v1_0/physics_parameters/ . Joint and surface soft-physics layers are distinct. https://everlastervr.github.io/docs/naturalis/v1_3/advanced_soft_physics/ describes local stretch/compression response and edge pressure gradients. https://everlastervr.github.io/docs/naturalis/v1_3/directional_force_morphing/ describes shape correction accompanying dynamics. We consulted author documentation, not proprietary plugin source; no claim to reproduce VaM soft physics.
4. Jacobson et al., Bounded Biharmonic Weights for Real-Time Deformation (2011): https://www.research-collection.ethz.ch/entities/publication/dca89378-4fa3-40ff-9e58-5b9dd3c240a0 . Smooth, shape-aware bounded weight fields matter. Our geodesic construction is NOT a bounded biharmonic solver.
5. Abu Rumman & Fratarcangeli, Position based skinning of skeleton-driven deformable characters (2014): https://mfratarcangeli.github.io/publication/sccg2014/ . Demonstrates a skin deformation layer with geometric constraints following skeletal skinning. We retain native GPU skinning and do not add a per-vertex runtime PBD layer.
6. Stecco et al., The anatomical and functional relation between gluteus maximus and fascia lata (2013): https://pubmed.ncbi.nlm.nih.gov/24139012/ . Six-cadaver study describing major fascial insertion/continuity. Barker et al., Anatomy and biomechanics of gluteus maximus and thoracolumbar fascia (2014): https://onlinelibrary.wiley.com/doi/full/10.1002/ca.22233 . Describes multiple proximal bony/fascial attachments. These support a broad connected support model, not a measured numerical spring calibration for this character. Passive pose response must not be called active muscle activation.

## Failure analysis

The first surface guard suppressed whole-side motion because it treated the weight gradient of common translation as an error. The residual-only revision restores common motion but cannot repair that gradient. The subsequent diffusion could only decrease weights inside existing support, so it could not create a wider transition outside the cut-off. It affected only 408/477 vertices with small changes and failed user observation.

The old profile also has different inferred material behavior on each side. Shape asymmetry is not inherently a bug. It is inappropriate, however, to interpret every noisy attachment estimate as independently measured tissue material. Old Core SI positive travel was 1.0263 versus 1.3125 cm. This is one contributor, not proof of the screenshot's complete cause.

## New implementation

### Geodesic surface transition

After final hip/leg influence redistribution, identify core seeds using each side's existing source-derived region evidence. Traverse the triangle graph (including source-ID seam links) through eligible pelvis/thigh donor support. Dijkstra edge lengths use imported appearance geometry in centimeters. The family policy specifies core confidence fraction and transition width relative to measured side height; no character or world-coordinate exception.

Set participation using a quintic compact fade of surface distance: `1 - (6t^5 - 15t^4 + 10t^3)`, t in [0,1]. The scalar fade has zero first/second derivative at its endpoints. Posterior and medial gates use the stable anatomical frame. Preserve at least 15% of eligible donor capacity. Reconstruct semantic kernel shares through existing donor compression, preserve non-donor leg/breast weights, honor eight influences, weld duplicate source vertices. The discretized distance field, donor capacity and influence selection do NOT have a global C2 guarantee.

This widens support into previously static neighbors. It does not shrink the global dynamic output. Geometry, topology, skeleton indices and morph delta arrays remain unchanged. All new data come from the existing automatic generation transaction. Build logs report core participation retention and maximum edge gradient before/after; these metrics are not visual acceptance.

### Bilateral material prior

New profiles opt into a shared dimensionless material response per semantic node: average the two baseline k/m vectors, and average positive/negative travel divided by side dimensions. Reconstruct stiffness with each side's own mass and travel with its own dimensions. Runtime preserves per-region pose tension and Shape updates. Both states and moving frames remain independent; there is no left/right positional locking. This is an explicit engineering prior, not anatomical proof that both sides are identical. Legacy profiles remain unchanged until Upgrade Runtime.

### Preserved and excluded

Amplitude remains 3 and the whole-glute common output is not attenuated. Regional residual protection remains. Original five dynamic semantics and pelvis/thigh structural support are retained. No Chaos, contact, continuous volume-conserving elasticity, hand-authored character corrective, or new independent physical nodes are introduced.

## Limits

A bone-based translation field is not a continuum soft-tissue model. Geodesic weighting can improve spatial handoff but does not guarantee no triangle inversion under arbitrary motion or large Shape edits. The extension also changes how structural helpers influence the outer skin; extreme poses need user observation. Source shape asymmetry remains. The bilateral prior does not force equal displacement when the two thighs move differently. No visual success claim is made.

## Spatial support correction during implementation

The first geodesic candidate reduced common-field edge gradients but failed the existing spatial support check on the second character's Medial helper (normalized center error 0.310 left, 0.254 right, limit 0.25). It was not published. The revised construction retains the original semantic helper ratios wherever support exists and uses new kernels only on newly reached vertices. It also keeps existing total participation as a lower envelope, subject to retained donor support. The geodesic field fills the surrounding transition rather than cutting away the medial support. Therefore no exact C2 claim applies to the maximum of these fields. Final metrics below refer only to the successful revision.

## Final semantic fit

Keeping prior helper ratios alone was insufficient on the second character after the total field changed. The final builder adds a positive multiplicative fit of helper shares toward their existing semantic rest centers. At each vertex it renormalizes ONLY the five hip shares to the unchanged geodesic total; non-hip weights, influence membership and total motion participation are unchanged. This prevents solving helper placement by moving original bones or reintroducing the old steep total field. It is a bounded-iteration engineering fit, not a proof of convergence or a reproduced BBW optimizer. The existing independent spatial-support validator remains unchanged and must accept both generated characters.

The intermediate lower-envelope candidate is superseded: total participation is the geodesic field capped by donor capacity, not max(old,new). The core participation audit determines whether amplitude was preserved. The 96 fitting passes are build-time only and create no per-frame vertex computation.

## Final delivery evidence

- Baseline HEAD b0adfe07b158c14e3c21d474a22ecf654d9a85c1, feature/jiggle/hip. Work remains uncommitted.
- Editor compile/link succeeded; Game Development compile succeeded (not a cook/package).
- Final primary BP: /Game/VamRuntime/R_f1accfde11c24242f4734a65/BP_VamCharacter.
- Final second character BP: /Game/VamRuntime/R_7d8f059e3096b8b0fc104635/BP_VamCharacter.
- Both build/reload/verify transactions committed. Existing eight-influence, bind, skeleton, profile and spatial validators remain enabled; no threshold was relaxed.
- Primary max common-field edge gradient 1.2891/1.2979 -> 0.2478/0.2502 per cm; core area-weighted participation 1.1316/1.1350 times baseline.
- Secondary gradient 1.7383/1.1473 -> 0.2177/0.2163 per cm; core participation 1.1443/1.1529 times baseline.
- Saved-profile audit: bilateral material flag true on both; paired baseline natural frequency error <= 2.3e-16 Hz; normalized positive travel error <= 1.4e-17. Actual physical displacement is not forced equal under different poses.
- Final maximum normalized helper/support-center error: primary 0.07324, secondary 0.15488 (unchanged acceptance limit 0.25).
- Evidence: Saved/LegJiggleT1/PrimaryGeodesic4, Saved/LegJiggleT1/SecondaryGeodesic4 and Saved/HipAsymmetry/final-evidence.json.
- Five installed Editor binary/module files SHA256-matched host build. Latest backup: I:/Document/UE5/SmartNPC/Saved/HipGeodesicFinalBackup/20260929-231932.
- No new automation test suite, cook or visual acceptance was performed. Engineering evidence must not be presented as proof that all visible folds are eliminated.
