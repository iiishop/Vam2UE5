# GPU soft contact search and conditional solve

Date: 2026-10-09. Branch `feature/chaos/breasts`, baseline `48795bd` plus working-tree changes. No commit or source character asset migration.

## Architecture

- Remove per-frame CPU spatial hashing, candidate enumeration, adjacency building and movement-invalidated pair cache.
- Cache an immutable balanced, threaded BVH forest per region when batch membership changes. CPU builds topology from source geometry only at that boundary. GPU refits each node from the current surface, in parallel groups of 64, then performs stackless nearest foreign-region triangle queries. World isolation and per-instance soft enable flags are checked on GPU.
- A fixed slot per source vertex replaces the unbounded CPU pair array. GPU linked reverse incidence lists gather equal-and-opposite barycentric corrections without float atomics or hash bucket overflow.
- At default settings, 12 searches replace 36 repeated selections. Each selected manifold is reused for three repairs, retaining up to 36 repairs for real contact. An extra selection may run for asynchronous load sampling; it is not counted as a solve round in diagnostics.
- GPU-generated indirect arguments dispatch zero groups for correction, gather, tet safety, safety reduction and apply when there is no actual penetrating contact. Broad/narrow search itself still costs work; this is not a zero-cost feature. Conditional repairs are batch gated, not fully compacted per contact island.
- Compile each PassKind as its own shader permutation, so unused resources and branches disappear. This also keeps the new search within the SM5 UAV limit.
- Diagnostics asynchronously expose searched rounds, active rounds and peak contacts. `GPU pair slots` is capacity, not candidate/contact count; `scene upload prep` is CPU preparation time, not GPU search time. A batch diagnostic shared by two actors must not be counted twice.

## Evidence

`Saved/ContactStage3GPU/compare.html` and `results.json` contain measured frame-time tables. Source/binary hashes accompany runs; old installed DLL is backed up in `install-backup`.

- Editor build: `editor-build3.log` succeeded. Game plugin compile: `game-build-final.log` succeeded.
- `Saved/ContactStage3/gpu-bvh-installed`: final installed GPUCoupling succeeded (world isolation, self contact, mirrored response, rigid shape forces, force balance).
- `Saved/ContactStage3/gpu-bvh-repair`: real two-character ContactInteraction succeeded. Final contact diagnostics: 12 active of 12 search rounds, 58 peak contacts.
- `Saved/ContactSkinResearch/stage3-gpu-repair`: real glass-contact regression and 180 UE screenshots. Volume error -0.04%, min J 0.3412, inverted 0, nipple RMS strain 0.05053, sampled render-vertex penetration 0 cm.
- Same independent host benchmark: 20.35 ms / 49.15 FPS before; 13.34 ms / 74.98 FPS held and 13.41 ms / 74.58 FPS moving after.
- Actual SmartNPC project baseline measured by temporarily restoring previous shader and DLL, restored in a finally block: 21.58 ms / 46.33 FPS held, 21.37 ms / 46.78 FPS moving.
- Actual installed optimized runs: held 15.37 and 14.60 ms (65.05 and 68.48 FPS); moving 15.00 and 14.81 ms (66.67 and 67.51 FPS). P95 17.61–20.30 ms; NOT a stable 60 FPS claim.
- All performance runs: two BPs separately pressed by two spheres, mutual contact enabled, 1920x1080 Quality 2, screen percentage 100, no VSync/frame cap, 300 measured frames per phase. No numerical-safety CPU fallback. They are not mutual-contact gameplay FPS, or a packaged game benchmark.
- No-contact performance scene reports 0 active / 12 searched: the conditional dispatch is actually exercised.

## Quality and remaining limits

No topology resolution, corotated material sweeps, volume barriers or skin bending budget was reduced. Simply cutting repairs to 12 was tried and rejected: it changed real pair displacement too much. The retained reused-manifold version yields peak pair displacements 0.8296 / 0.9475 cm versus 0.8206 / 0.9595 cm before. Estimated soft reaction peak changes from 10.38 to 7.57 N; force balance remains zero. The schedule/query changes are not bitwise or trajectory equivalent and should not be called proven visually lossless. Rigid reaction/upper-body offset are approximately unchanged at 16.12 N / 1.417 cm.

The original model limitations remain: discrete contact, nearest triangle per point, no soft CCD, friction, universal cloth coupling, guaranteed thin/edge-edge contact or arbitrary deep initial overlap. Reverse-list floating-point summation order can vary. BVH refit is O(F log F) work with independent node reductions; it is a practical small-cage design, not a scalability claim for unbounded crowds. Current gating is batch-wide once actual contact exists. The GPU search itself was not separately timestamp-profiled in this change.

No Cook or packaged-executable test was rerun in this optimization. Visual acceptance remains with the user. Full-scene stable 60 FPS needs further budgeting.
