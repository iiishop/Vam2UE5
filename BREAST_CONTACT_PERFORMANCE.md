# Breast contact performance experiments — 2026-10-08

## Scope

Preserve the accepted fTetWild contact appearance. Prefer native UE 5.8 options.
No source character assets, material calibration, pressure trajectory, mesh density,
volume target, or test tolerances were changed. The final validated default is installed after backing up the previous DLLs.

## Reproduction and evidence

Isolated Editor host: Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject.
Profile: /Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime.
Saved/ContactPerformance/run.py runs variants sequentially, with real GPU skinning.
Saved/ContactPerformance/analyze.py compares GPU positions against baseline.
Logs, per-case summaries, raw float32 XYZ snapshots and results.json are retained
under Saved/ContactPerformance. No AI-generated imagery is used.

Each reported frame average is the mean of seven 90-tick pressure phases, including
world/character/render work. It is NOT the isolated solver duration. Solver detail
counters separately report native material, volume constraints, and post-contact.
Baseline is repeated to check drift. Individual phase spikes remain in the raw data.

Four GPU readbacks cover zero pressure, two left-platen samples, and actual world
sphere pressure. They compare every render vertex. They are not an exhaustive
per-frame trajectory comparison or a visual acceptance test. Existing runtime tests
also exercise right pressure, off-center pressure, release, translation, rotation,
material retention, local J, volume, surface strain and penetration.

## Experiments

- Native GS Neo-Hookean: faster, but changes material response. Reject if geometry
  differs substantially or existing physical checks fail.
- Native task batch size 32: changes engine scheduling only; no project-wide CVar
  default is persisted. The original batch settings remain in normal use.
- Native batch threshold 100000: serial scheduling comparison, not a production
  recommendation.
- Native solver iterations 8 and 6: skin bending receives the same iteration count
  as the solver. Compare visible output, not merely engineering pass/fail.
- Static cache: retain face incident lists and compute rest edge lengths once per
  post-contact callback. Does not cache stale collision normals or positions.
- Boundary volume: oriented closed boundary sum replaces tetrahedral global-volume
  and gradient accumulation. Local cell safety, global target and backtracking are
  retained. Internal material simulation remains volumetric.

## Runtime controls (final defaults)

vam.Contact.NativeNeoHookean = 0
vam.Contact.NativeIterations = 0 (use Profile's 12)
vam.Contact.CacheStaticData = 0
vam.Contact.BoundaryVolume = 1 (fTetWild profiles only)

Flags apply when the contact solver initializes; reset it after selecting a variant.
Only boundary-volume evaluation is adopted for fTetWild profiles. Legacy profiles
keep their previous summation. Other experimental options remain disabled.
The timing probes and GPU test output make subsequent optimizations measurable.

## Deliberately not enabled

Threaded advance cannot safely be enabled on this manually stepped, UObject-reading
callback path without snapshot/result ownership and synchronization changes.
Arbitrary local freezing or lower update frequency can change load propagation and
latency. They are not treated as harmless options. GPU solver replacement and
constraint graph-coloring are larger implementations, not validated by these tests.
No speedup is claimed for any of these unimplemented approaches.


## Results and decision

| Variant | Mean pressure test frame ms | Maximum sampled GPU difference | Decision |
|---|---:|---:|---|
| Baseline, two runs | 51.398 / 51.466 | identical | reference |
| Native Neo-Hookean | 39.997 | 16.957 mm | reject: stretch/penetration checks fail |
| Native batch size 32 | 57.005 | identical | no demonstrated benefit; includes spikes |
| Native serial batch threshold | 51.053 | identical | no meaningful improvement |
| Static caches | 52.499 | identical | no measured improvement |
| Native iterations 8 | 36.170 | 6.313 mm | reject despite engineering pass |
| Native iterations 6 | 29.727 | 13.029 mm | reject despite engineering pass |
| Boundary volume, two runs | 50.590 / 48.510 | 0.000632 mm | adopt only for fTetWild |

Both boundary-volume runs pass existing tests and produce the same sampled GPU
positions as each other. Average across the paired runs is about 3.7% faster than
the paired baseline, with individual comparisons about 1.6--5.7%. This is a small
host-specific result, not a general FPS guarantee or a solved performance problem.

The speedier material and iteration variants fail the preserve-appearance objective.
No tolerance was relaxed to accept them. Whole-frame figures should not be confused
with the earlier isolated contact CPU figure (~39--47 ms). Native material remains
the largest measured part (~21--29 ms in ordinary sampled frames); volume projection
is several milliseconds and post-contact ~12--15 ms. Raw logs preserve outliers.

No sleep, update-rate reduction, coarse physics mesh, material recalibration,
async UObject access, or engine-wide batch setting has been adopted. Final main-project Editor binaries were installed after verifying no interactive
UE editor was running. Five files are SHA-256 matched to the tested host binaries;
previous files are saved under Saved/ContactPerformance/installed-backup.


Final default-path verification: ContactRuntime succeeded; mean pressure frame
48.210 ms, maximum sampled GPU difference 0.000063152 cm. No main Content assets
were overwritten or deleted. Existing C4 profiles are excluded from the new default
boundary sum. Experimental C5 BP remains R_619448199d803edf1ab83af9/BP_VamCharacter.
The fTetWild build selector itself remains opt-in. Installation hashes are recorded
in Saved/ContactPerformance/installation.json. No Game/cook run was performed here.

## Callable contact toggle and disabled benchmark (2026-10-08)

BP_VamCharacter exposes Set Breast Contact Enabled(bool) and Is Breast Contact Enabled.
The BreastContact component also exposes Set Contact Enabled / Is Contact Enabled;
Blueprint writes to Enabled use its setter. False immediately releases solver and
contact surface resources, restoring the preceding mesh deformer. Jiggle settings
are untouched. True allows normal demand-driven initialization on the next tick.
Reset Contact now preserves Enabled. Existing defaults remain enabled; call false
at BeginPlay or configure the component default false for an opt-in character.
Disabling discards the current contact deformation; re-enabling can have an
initialization cost. This is not a seamless paused-state resume. Debug/external
source configuration is retained. C++ callers should use the setter.

Editor build succeeded; installed with timestamped toggle-backup and SHA256 verification.
Three independent UE processes, 600 measured ticks each after 120 warm-up ticks.
One C5 character, breast Jiggle explicitly enabled, contact toggled on then off;
zero solvers, restored native mesh path, preserved materials and reset-off behavior
asserted. All three ContactRuntime benchmark branches passed.
256x256 offscreen SceneCapture with per-frame rendering flush; idle/breathing disabled
and accessory renderers hidden by the existing controlled harness. This measures
world tick plus synchronized capture, not production viewport or pure character CPU
cost; excludes representative game scene, clothing/hair simulation and gameplay.
It cannot establish a stable full-game frame-time guarantee or four-character FPS.
No Game/cook or full pressure regression was run for this toggle-only update.

- Run 1: CONTACT_DISABLED_BENCH samples=600 mean_ms=4.895 p50_ms=4.793 p95_ms=6.562 p99_ms=7.200 max_ms=10.543 Jiggle=on solvers=0
- Run 2: CONTACT_DISABLED_BENCH samples=600 mean_ms=4.898 p50_ms=4.796 p95_ms=6.543 p99_ms=8.144 max_ms=8.673 Jiggle=on solvers=0
- Run 3: CONTACT_DISABLED_BENCH samples=600 mean_ms=4.978 p50_ms=4.844 p95_ms=6.758 p99_ms=7.726 max_ms=8.458 Jiggle=on solvers=0

Raw logs and runner: Saved/ContactPerformance/disabled-1..3 and run-disabled.py.

## Round 2: contact query and native solver experiments

Baseline commit: 46c0e7b. No source/runtime assets were regenerated or overwritten.
Reproduction: Scripts/run_contact_query_experiments.py baseline bounds cache both
baseline2 dirty boundsdirty ab xpbd final legacy; build host from current source first.
Scripts/analyze_contact_query_experiments.py writes Saved/ContactPerformance/broadphase/results.json.
The scripts use this workstation's existing isolated host and C5/C4 configurations.

Implemented switches (read when the solver constraints initialize; Reset Contact to apply):

- vam.Contact.Broadphase=1: immutable collider world AABBs expanded by contact thickness
  plus 0.02 cm. Every point/face query checks CURRENT positions; no stale per-frame
  candidate list. Unbounded geometry falls back to the original exact query.
- vam.Contact.DirtyConstraints=1: incident-edge and incident-tet lists mark constraints
  dirty after any node write. Preserve original forward/reverse edge order and tet
  order. Clear flags when evaluated; propagate writes to all neighbors. The final
  sweep's different bulk correction evaluates all cells. Global volume, bending,
  cell safety and final face projection are unchanged. All flags restart each callback.
- vam.Contact.ExactQueryCache=0: experimental point query memoization, valid only for
  identical positions within one callback. Tested but slower; not adopted.
- vam.Contact.NativeXPBD=0: experimental native corotated XPBD instead of GS.
  Its separate material/attachment rule ranges are serialized in their original order
  before the project's volume constraint to avoid concurrent writes. This changes
  convergence and is NOT the production solver. Tests passed but shape difference
  was unacceptable, so it remains off. Original material/12 iterations are retained.

Independent-process pressure-frame averages (ms): baseline 44.123; repeat 50.393;
bounds 42.822; cache 46.732; bounds+cache 44.759; dirty 46.026;
bounds+dirty 44.089; XPBD 41.774. This drift rules out interpreting the best run as
an improvement ratio. All non-XPBD variants matched the original four GPU snapshots
bit-for-bit; XPBD differed by up to 1.3226899 cm despite passing engineering thresholds.

Same-process ABBA used an actual world-collision glass sphere, no debug press source:

| Run | Moving ms | Held ms |
|---|---:|---:|
| Baseline A0 | 47.039 | 48.938 |
| Bounds+dirty B1 | 46.510 | 47.316 |
| Bounds+dirty B2 | 46.733 | 47.453 |
| Baseline A3 | 47.662 | 47.472 |

Paired means: moving 47.3505 -> 46.6215 ms (~1.54%); held 48.205 -> 47.3845 ms
(~1.70%). Modest machine-specific evidence, not a statistically established universal
speedup. Four additional full-press GPU snapshots match bit-for-bit. Every ABBA
pressure state passes existing volume, inversion, surface stretch, displacement and
material checks. Timings include world update plus synchronized 256x256 rendering.
No visual acceptance or full-trajectory/all-character equivalence is asserted.

We retain only bounds+dirty as the production defaults. Material, compliance, solver
iterations, topology and surface binding are unchanged. No equilibrium sleep,
adaptive timestep, reduced mesh, asynchronous threading or new contact physics was
implemented. Current near-40-ms solver cost is not solved; native material work still
dominates. Future invasive solver or mesh experiments must retain geometry comparisons,
not just engineering pass/fail.

Final default-path ContactRuntime passed with GPU: mean pressure frame 41.602 ms,
four baseline GPU snapshots bit-identical. This faster isolated run is NOT used as
a speedup ratio because of demonstrated process-to-process drift. Legacy C4
ContactRuntime also passed in NullRHI (no GPU shape comparison for legacy).
Editor build and git diff --check passed. No Game/cook run this round.
Installed Editor DLL/PDB/module files after checking no interactive Editor and matching
BuildId, with all five SHA256 hashes verified. Backup:
Saved/ContactPerformance/query-backup-20261008-224910; installation.json is under broadphase.
No assets regenerated, no Jiggle changes, no commit/push performed.

## Unreal Insights diagnosis (2026-10-08)

Added per-stage TRACE_CPUPROFILER scopes, and test timing regions for pressure phases
and real world-sphere move/hold/release. No per-particle logging. Captured CPU, frame,
region and task channels; exported tables with UnrealInsights -NoUI -AutoQuit and
TimingInsights.ExportTimerStatistics. Raw evidence: Saved/ContactInsights (~270 MB
per trace), CSV tables, results.json and compare.html. Reproduction scripts:
Scripts/profile_contact_insights.py reference workers4 workers8;
Scripts/analyze_contact_insights.py. The original reference capture is at the evidence
root (baseline.utrace); rerunning reference creates a separate reference subdirectory.

GameThread inclusive time normalized by ACTUAL contact ticks (90 moving, 92 held,
including two held readback ticks):

| Scope | Moving ms | Held ms |
|---|---:|---:|
| Contact component total | 33.309 | 35.730 |
| Native GS main constraint (material + attachments) | 20.277 | 21.060 |
| Volume constraints | 3.345 | 3.685 |
| Post-contact constraints | 6.527 | 7.802 |

Held post-contact subscopes: skin bending/backtracking 1.942 ms; edges 1.787 ms;
cell barrier scans 1.330 ms; triangle contacts 2.009 ms; convergence checks 0.594 ms.
These are children of post-contact, not additional costs to add to the parent.
Native calls are 12 per contact update; local scans are 48 per update in sphere
cases, i.e. the minimum four per native iteration, not continuously at 24-sweep cap.
Cross-thread inclusive sums are NOT frame time. Trace overhead is not subtracted.

Correction to earlier batching experiments: the current FDeformableSolver creates
FGaussSeidelMainConstraints without passing GDeformableXPBDCorotatedParams. Its
constructor therefore receives default batch parameters. Changing XPBDBatchSize or
XPBDBatchThreshold did not tune this main GS loop. Do not cite those old results as
proof that main-solver granularity tuning cannot help.

A genuinely connected control is p.Chaos.MaxNumWorkers, which sets minimum work
batch size in Chaos::PhysicsParallelFor (not a literal fixed thread count).
Isolated process experiments passed and had bit-identical GPU samples:

| Setting | Held component ms | Held native GS ms |
|---|---:|---:|
| Default 100 | 35.730 | 21.060 |
| 4 | 38.280 | 23.911 |
| 8 | 36.713 | 22.122 |

Neither improved this workload, so no global physics setting was persisted.
The profiler identifies the dominant routine, but does not separate internal stress,
polar decomposition and Hessian costs: this installed build has no Chaos PDB and no
per-function trace scopes there. Source-level reasoning about these calculations
must not be described as measured exclusive timings. Further invasive optimization
should target that native routine, using a source-built/appropriately instrumented
engine or a validated native-class integration, rather than repeat generic cache
or debug-display experiments. No 1-2 ms performance target is claimed achieved.
Editor build and all three ContactRuntime runs passed. Instrumented default GPU
samples match the last installed default. No Game/cook run for these profiling changes.

## Native GS kernel investigation (2026-10-08, follow-up)

Implemented `Vam.Breast.ContactKernel`, using the installed native corotated class
through a diagnostic subclass of its supported protected callbacks. This avoids
private-layout hacks or rebuilding/replacing Chaos. Production simulation is not
changed. It is an explicit benchmark requiring `-VamBreastTestConfig=...`.

The benchmark uses the C5 profile's real topology, profile rest coordinates and
uniform profile material, with three frozen synthetic deformation fields. These
are NOT snapshots from the pressed runtime character, and omit runtime spatial
material calibration. Seven warmed repetitions, 24 traversals each, alternate
ablation order. Timings are per active vertex/tet incidence (serial CPU work):

| Field | Full call ns | Removing stress saves ns | Removing Hessian saves ns |
|---|---:|---:|---:|
| Rest | 212.443 | 120.992 | 16.505 |
| Compression/shear 1 | 351.027 | 260.432 | 16.553 |
| Compression/shear 2 | 355.154 | 265.216 | 12.906 |

Nonzero deformation makes stress about 74% of this kernel's ablation cost; Hessian
is about 4-5%. Replacement no-op callbacks retain dispatch overhead. These are
difference measurements, not exact exclusive timers, and NOT fractions of total
frame time. Source inspection confirms PCorotated invokes SVD polar decomposition;
the experiment does not separately time the polar and remaining stress arithmetic.
Do not remove stress/Hessian from a real solve: ablations are diagnosis only.

Constructed the public native FGaussSeidelMainConstraints directly for a separate
material-only problem, preserving color order and constitutive law. Its constructor
DOES accept batching; the current FDeformableSolver owner does not expose/forward
this instance setting. Tested 12 GS iterations per solve, warmup + seven repetitions,
then repeated batch 1 and 5 in reverse order:

| Batch | Median 12-iteration ms | Max endpoint error cm |
|---|---:|---:|
| 5 (native default) | 20.721 | 0 |
| 1 | 16.445 | 0 |
| 16 | 26.689 | 0 |
| 32 | 37.131 | 0 |
| 64 | 88.875 | 0 |
| 512 | 87.219 | 0 |

Batch 1 saved about 20.6% in this microbenchmark. This excludes weak attachments,
custom volume and contacts, and checks only the 12-iteration endpoint. It is a
promising integration candidate, NOT an installed character performance gain.

A full real-glass-sphere ContactRuntime capture with process-local
`p.Chaos.DisablePhysicsParallelFor=1` passed all existing engineering checks and
four GPU snapshots were bit-identical to reference. Held contact component cost
was 95.956 ms, native GS 81.455 ms (previous default 35.730 / 21.060 ms).
Moving cost was 89.577 / 76.766 ms. Thus disabling parallelism is rejected.
No global CVar is persisted or changed in a user's running editor.

Research informing next candidates:
- https://arxiv.org/abs/2306.09021 — PBNG, quasistatic hyperelastic vertex GS.
- https://ankachan.github.io/Projects/VertexBlockDescent/index.html — vertex block
  descent and parallel elastic solves; published GPU results are not UE timings.
- https://matthias-research.github.io/pages/publications/stablePolarDecomp.pdf —
  robust rotational extraction; an alternative to evaluate, not implemented here.
- https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview —
  Epic's low-resolution runtime / high-resolution rendering design.

Next integration should expose a per-instance native batch setting or use a
validated native-class adapter preserving all native attachments/init/transients.
Do not globally flip physics CVars inside callbacks. Then evaluate SIMD/batched
stress or error-controlled rotational extraction with singular/inverted fallbacks.
Caching across vertex updates is not mathematically equivalent: a neighboring
vertex update changes that tetrahedron's deformation gradient. A GPU approach
must preserve color barriers and handle CPU/GPU synchronization explicitly.

Evidence: Saved/ContactInsights/kernel/compare.html, timings.csv, batch CSV,
runtime.log and serial-results.json; full serial trace under ../serial.
Reproduce: Scripts/run_contact_kernel.py then Scripts/report_contact_kernel.py;
Scripts/profile_contact_insights.py serial for the full-contact control.
Editor compile, ContactKernel and serial ContactRuntime passed. No production
DLL installation, asset rewrite, Game build, cook, commit or push in this follow-up.
Current production contact cost remains unresolved; no visual acceptance claimed.

## 2026-10-09: native GS adapter integrated and alternatives compared

Integrated a private single-proxy adapter built from UE native
FGaussSeidelMainConstraints, corotated tetrahedral material and weak attachment
constraints. Default `vam.Contact.NativeGSBatch=1` applies only to the owned
fTetWild breast contact path. `0` retains the original engine owner; `5` uses
the original batch size through the adapter. Reset contact after changing it.
No global worker/physics settings are persisted, no mesh/material/iteration or
Jiggle settings changed, and no character asset regeneration is needed.

Preserves original incident ordering, initial rest coordinates (NOT already
advanced particle positions), target data, per-step weak-constraint initialization,
SOR and native displacement/parallel CVars. Mutable constraints are per evolution.
Original native owner remains allocated with its original initialization callbacks;
only its Apply rule is replaced. Unsupported layouts fall back before mutation.
The adapter is specific to this component's private solver, not a general arbitrary
Chaos collection extension (no extra muscle, grid, native volume or spring-contact
constraints). Re-audit if that private solver's construction changes.

Full ContactRuntime with real glass sphere and GPU readback, Unreal Insights
GameThread inclusive scope divided by actual component ticks, held phase:

| Candidate | Component ms | Native GS ms | Four sampled GPU vertex arrays |
|---|---:|---:|---|
| Adapter batch 5 control | 36.206 | 21.319 | bit-identical to original owner |
| Adapter batch 1 | 32.382 | 17.415 | bit-identical |
| Original owner, suppress high-frequency Flesh logs | 36.695 | 21.708 | bit-identical |
| Original owner, cache rest-volume displacement limits | 36.894 | 21.957 | bit-identical |
| Batch 1 + Newton polar candidate | 29.900 | 14.951 | max error 0.786625 cm: REJECTED |
| Batch 1 + rest-limit cache | 33.255 | 18.099 | bit-identical |
| Final default, fresh process repeat | 32.108 | 17.419 | bit-identical |

Only batch 1 is enabled by default. Rest-limit caching reduced local compression
from ~1.82 to ~1.61 ms, but no additional whole-component gain was demonstrated;
`vam.Contact.RestMetrics=0`. `vam.Contact.ExperimentalPolar=0`: this bounded Newton
polar candidate has native SVD fallback, but passing engineering thresholds did
NOT establish shape equivalence. Do not ship it enabled. Initial adapter attempts
also differed by ~7.9 mm and were rejected; correcting rest construction, incident
order and weak initialization restored equivalence before batching comparisons.

Final default remaining scopes: native GS 17.419 ms, custom volume total 3.757 ms,
post-collision total 7.752 ms. Their nested child timers must not be added again.
The component's ~32 ms is NOT an entire game frame or four-character performance.
Separate-process timings have noise; no confidence interval or hardware-independent
speedup claimed. Four sampled snapshots do not prove every frame/pose equivalent.

Final Editor build and ContactRuntime passed (glass contact, penetration/volume/
inversion/stretch thresholds, material and toggle/reset regressions). No adapter
fallback appeared in the final run. No Game build, cook or visual acceptance this
round. Existing nonfatal Flesh TargetDeformationSkeleton warnings remain.

Evidence: Saved/ContactInsights/native-adapter/compare.html and results.json;
full production trace/runtime.log in Saved/ContactInsights/production. All failed
candidates are retained separately for audit; no accepted baseline was overwritten.
Reproduce: Scripts/profile_contact_insights.py fixed5 fixed1 quiet metrics polar
combined production; Scripts/report_contact_runtime_experiments.py.
The earlier adapter5/adapter1 folders are intentionally the rejected early runs.

Research used to guide the stress experiment (not a claim to reproduce the paper):
- https://animation.rwth-aachen.de/media/papers/fast-corotated-fem-using-operator-splitting/2018-SCA-FastCorot.pdf
- https://github.com/InteractiveComputerGraphics/FastCorotatedFEM
- https://matthias-research.github.io/pages/publications/stablePolarDecomp.pdf
- https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview

Next targets justified by measured cost: equivalent SIMD/batched stress evaluation,
then exact local contact/volume work reduction. Cross-vertex stress caching is not
safe because each neighboring update changes the deformation gradient. Current
Newton replacement must be corrected/validated before any performance adoption.

## 2026-10-09: two-character actual game-loop frame benchmark

User requested measured FPS, not an inverse estimate of component timing.
Added opt-in non-Shipping `vam.ContactBenchmark` developer command and
Scripts/run_contact_two_characters.py / report_contact_two_characters.py.
Only compiled into the isolated host for this measurement; production binaries
and assets were not replaced in this follow-up.

Hardware: Intel i5-12600KF, NVIDIA RTX 4070 Ti. UE 5.8.3 Development Editor
-game, DX12 offscreen game viewport, High sg=2, screen percentage 100, no VSync
or FPS cap. Two real BP_VamCharacter instances using the existing C5 RC.
Normal actor/component ticks, normal viewport rendering, Jiggle left at defaults.
Entry simple scene, fixed camera/two lights; existing imported clothing geometry
is present, but no added hair/cloth simulation or full-game systems.
Two independent kinematic sphere colliders press one breast on each character.
This is NOT inter-character soft-soft collision or four breasts pressed at once.

Per phase: 180 initial warmup frames (120 subsequently), then 300 measured
normal game frame intervals. Screenshots requested 40 frames before measurement;
no synchronous SceneCapture or GPU geometry readback during measurement.
FPS = 1000 / mean elapsed frame milliseconds, not mean of reciprocal FPS.

| State | Run 3 mean ms / FPS | Run 4 mean ms / FPS |
|---|---:|---:|
| Contact off, Jiggle enabled | 3.752 / 266.5 | 3.529 / 283.4 |
| Contact on without sources, automatic idle | 3.498 / 285.9 | 3.840 / 260.4 |
| Two characters simultaneously pressed | 74.844 / 13.36 | 75.128 / 13.31 |
| Contact switched off after press | 3.707 / 269.7 | 3.717 / 269.1 |

Pressed p95 frame time: 81.60 / 81.51 ms. Every sampled pressed frame records
2 active solvers, other phases 0. No contact inversion/unsupported layout errors
in these runs. This does not constitute visual acceptance or a performance
promise for a larger scene, packaged Shipping build, or display presentation.

The first two trials requested 1080p but screenshot/log verification exposed an
888x500 automatic window resize. They are preserved as preliminary evidence and
EXCLUDED from the final results. Explicit r.SetRes and -ForceRes corrected it;
run3/run4 screenshot pixels and logged viewport dimensions confirm 1920x1080.

Evidence: Saved/ContactTwoCharacters/compare.html, results.json, run3/run4 raw
frames.csv, command.json, runtime.log, and phase screenshots. Low no-contact cost
comes from the existing idle path releasing Chaos solvers and restoring native
skinning; it must not be advertised as active soft-body solver performance.

## 2026-10-09: memory-for-time experiments

Implemented two native GS material candidates under `vam.Contact.MaterialCache`:
1 caches the rest-volume-weighted inverse transpose; 2 also caches stress keyed
by all nine numerical deformation-gradient components. Native stress and Hessian
callbacks remain in use. No tolerance-based stale reuse or iteration reductions.
Each material/evolution owns its cache. Shape/reinitialization destroys it. Native
static coloring ensures no two vertices of one tetrahedron execute concurrently;
this assumption must be re-audited if solver coloring/scheduling changes.

Both candidates passed ContactRuntime and all four sampled GPU arrays matched
original-owner reference bit-for-bit. Existing engineering checks cover pressure,
volume, inversion, penetration, toggles and translated/rotated cases. This is not
an all-frames/all-poses equivalence proof. Cache 2 remains opt-in, with
0 the retained default after the noisy final repeat. Both current cache modes allocate 1,195,488
bytes per profile instance (1.14 MiB); no dynamic state is shared between people.
Cache 2's aggregate completed-instance hit fraction was 45.38% in the first full
regression. Static material data is fixed at construction; changing constitutive
parameters in place would require invalidation/reconstruction.

Single-component held phase: prior production 32.108 ms, geometry 31.574 ms,
exact memo 29.810 ms. Moving: 29.981 / 28.889 / 26.859 ms respectively.
The previous production sample is from an earlier process/time; do not treat its
small differences as a controlled statistical speedup guarantee.

Extended real-game two-character benchmark with fresh same-round control:
- static press: no cache 83.996 ms / 11.905 FPS; exact 74.515 ms / 13.420 FPS;
- continuous sinusoidal moving sphere: 77.114 ms / 12.968 FPS vs
  75.120 ms / 13.312 FPS;
- two active solvers in every measured pressure frame; release returns to
  existing zero-solver idle in both paths (not a new release dynamics model).
- Earlier static runs were ~75 ms even without cache, indicating run-to-run noise.
  Do not claim a large FPS gain. Target 60 FPS remains unmet.

Also ran an OFFLINE matrix preflight on exported actual C5 profile (1380 nodes,
5337 tetrahedra, 87 fixed, 1293 free): assembled scalar linear FEM stretch block,
Dirichlet-eliminated fixed nodes, sparse LU factorization, three RHS backsolve.
Assembly 15.124 ms; factorization 5.292 ms; median backsolve 0.185 ms; factor data
869,432 bytes; linear residual 2.0e-15. Python/SciPy 1.14.1, one BLAS thread.
This is NOT an integrated Projective Dynamics or complete contact solver: no
rotational local step, volume/contact solve, weak attachment model, or skin output
is included. It cannot predict full runtime FPS or preserve current nonlinear
constitutive response by itself.

739 free surface nodes / 1293 free nodes = 57.15%. Thus the paper's favorable
small collision-prone subset (~5%) does not match this cage. A scalar float64
dense surface Schur matrix alone would use 4,368,968 bytes; full nonlinear/contact
cost is additional. Do not prioritize that method on the strength of the paper's
published speed alone.

Reproduce: Scripts/profile_contact_insights.py geometry memo cache-final;
Scripts/run_contact_two_characters.py cache-control 0 and cache-exact 2 (the mode
argument also enables moving/release phases). This benchmark exports mesh.json;
copy it to Saved/ContactPrecompute/mesh.json, run preflight_contact_matrix.py then
report_contact_precompute.py. Assets are only read, never saved/rewritten.
Evidence: Saved/ContactPrecompute/compare.html and raw result JSON; exact/control
CSV and screenshots remain under Saved/ContactTwoCharacters. Editor compiled;
no Game build, cook, visual approval, commit or push in this experiment.

Final candidate repeat (cache-final) passed engineering/GPU equivalence again, but
held component cost was 33.774 ms (GS 16.862), moving 27.797 ms. This did not
replicate the first held gain. Cache default was therefore restored to 0; no
production DLL replacement. The profile script explicitly selects mode 2 for
cache-final so the preserved experiment remains reproducible. More controlled
ABBA repetitions are needed before claiming stable end-to-end gains.
