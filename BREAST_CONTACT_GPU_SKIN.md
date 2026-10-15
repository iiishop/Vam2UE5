# GPU skin contact target — 2026-10-09

Baseline HEAD: `8563d98`, branch `feature/chaos/breasts`. User target: approximate the existing **Native CPU + skin bending** comparison on GPU; no additional mesh refinement.

## Adopted runtime

- Resident runtime now uses vertex-colored corotated material updates. Stress and positive local Hessian approximation follow the native Chaos GS corotated formulation, with an eight-step polar iteration rather than native SVD. This is not bitwise native-solver equivalence.
- Young modulus, Poisson ratio and chest-wall attachment strengths come from the character profile using the same unit conversion and foundation support function as CPU.
- Skin rest-dihedral constraints, bounded triangle/sphere contact and volume-aware global-per-side backtracking operate on the existing cage. No new physical nodes or render triangles.
- Nonlocal source-supported nipple distance graph matches CPU membership. It retains 3D shape while allowing movement; no fixed world-space nipple pin.
- Each handle retains its own prior GPU residual and collider positions. Animated rest is rebased each frame. Prior buffers remain read-only; current output is a new batch buffer. Spawn/batch offsets are remapped per handle. Existing Release on ShapeRevision/TeleportRevision/Reset discards history.
- Persistent quasistatic state is essential: rebuilding from rest every frame did not approach the CPU reference even after adding skin constraints.
- Default material sweeps: `vam.Contact.GPUIterations 8`; post-contact barrier budget remains 128. `GPUSkinBendingRelaxation=0.2` is an engineering Jacobi relaxation coefficient, separate from CPU authored `SurfaceBending=0.05`; those numbers are not directly comparable tissue parameters.
- No synchronous GPU readback in the runtime solver. Diagnostics remain asynchronous and sampled. Benchmark geometry readbacks are validation-only.

## Evidence

`Saved/ContactSkinResearch/gpu-skin-8`: existing ContactRuntime glass-sphere trajectory, 180 frames / six seconds, test Success. At the measured held frame: left cage volume error -0.04%, min J 0.3412, zero inversions, nipple RMS strain 0.05054, sampled masked render-vertex penetration 0 cm. This is not a guarantee of zero penetration over every surface point/frame. No CPU fallback recorded.

CPU reference `cpu-shell`: same scene/trajectory, nipple RMS strain 0.04612 and held-frame render penetration 1.67 mm. CPU is the requested comparison, not anatomical ground truth.

`Saved/ContactGPURuntime/skin8`: two characters, Entry benchmark, 1920x1080, quality 2, resolution scale 100%, vsync/frame cap off; 300 measured frames per phase after warmup.

| Phase | Mean ms | Mean-derived FPS | P95 ms |
|---|---:|---:|---:|
| Held pressure | 12.504 | 79.98 | 13.556 |
| Moving pressure | 12.548 | 79.69 | 13.754 |

Both actors reported GPU resident, zero native solvers. Actual output geometry captured. No safety fallback. This is benchmark-scene performance, not a full game budget with clothing/hair and complex scenery.

32 sweeps were also measured (`skin32`): 21.376 ms / 46.78 FPS held. The retained warm-start 8-sweep configuration passed the same pressure test; reducing work did not mean removing contact or shortening the stroke.

## Use and limits

Existing GPU candidate `/Game/VamRuntime/GPUContact_20261009_v2/BP_VamCharacter_GPU` and its RC/profile remain usable without rebuilding assets. Reload the editor after DLL installation. Contact enabled + GPU backend uses the new path; existing Reset clears persistent state. Existing profile assets obtain the new default GPU bending property on load.

No new asset migration, mesh subdivision, materials, nipple geometry or Jiggle changes. This remains the existing sphere-supported GPU contact scope. Residual skin creases remain, as in the selected CPU reference. No visual or biomechanical validation is claimed. Final appearance is for user review.

The earlier `BREAST_CONTACT_SKIN_RESEARCH.md` records rejected experiments from the previous request; this implementation supersedes its then-current delivery status.

## Installed verification

Editor build succeeded. Installed all three matching plugin DLLs and module manifest into SmartNPC, preserving prior files under `Saved/ContactGPUSkin/install-backup`; hashes saved beside that folder. No user assets changed.

Ran existing ContactRuntime capture through the actual `SmartNPC.uproject`, without an iteration override: Success. `gpu-skin-installed` contains 180 frames / six seconds. Held-frame left volume error -0.04%, min J 0.3412, zero inversions, nipple RMS strain 0.05056, measured masked render-vertex penetration 0 cm. No GPU safety fallback recorded. Final animation page: `Saved/ContactGPUSkin/compare.html`.

No commit made; HEAD remains 8563d98 plus working-tree changes.
