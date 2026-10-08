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
