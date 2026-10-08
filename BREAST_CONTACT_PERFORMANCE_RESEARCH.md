# Breast Contact performance and collision investigation — 2026-09-30

## Scope and evidence
Research only in this pass: project code, installed UE 5.8 source, last user PIE log,
and Epic online documentation. No scene profiling, automated tests, new DLL install,
or claim of repaired performance. HEAD remains 2922d9f with existing local changes.

`Saved/BreastChaosC1/performance-log-evidence.json` contains log cadence statistics:
1314 sparse vertex update records, 1254 adjacent-frame intervals, median 108 ms.
The final visible run includes consecutive ~330 ms intervals. These are timestamps
of log events, NOT attribution of CPU/GPU frame costs. The log does not record the
checkbox transitions or actor identities, so it cannot establish whether a disabled
instance continued simulating.

## Confirmed code properties
- Two synchronous Chaos solvers, 120 Hz accumulator, max 8 substeps, 12 iterations:
  up to 192 solver iterations per rendered frame, before per-element work.
- Quasistatics are enabled, yet the orchestration still performs time catch-up.
  A slow frame requests more work up to the cap. No idle/contact sleep path exists.
- The custom volume rule allocates per-side gradient arrays and recomputes authored
  rest volume inside every constraint iteration. Rest volume can be cached and
  gradient storage reused without changing physics.
- Both solvers write/read collections every substep; output is copied again into a
  render buffer. Surface upload calls UE UpdateBuffer/BeginUpdateResourceRHI.
  This is resource updating, not proof of complete asset reconstruction each frame.
- CurrentActor returns cached DebugActor or editor selection without resolving PIE
  counterpart or checking world. Wrong-instance toggles are possible, not confirmed
  for the user's specific checkbox clicks. PIE edits also do not persist to the next
  play session by default.
- Release destroys solvers and explicitly resets their simulation proxy. Therefore
  continuing simulation of the SAME successfully released component is not established.
- Engine DisableSimulation contains a suspicious inverted Contains guard when
  removing ConnectedObjects. Our full solver destruction mitigates that path; this
  is not evidence of the user's persistent slowdown by itself.

## Contact topology and quality
Epic documents vertex-based rigid collision. Current builder takes a convex hull
of breast support vertices and chest-wall projections, then fans triangles into a
single center. It discards non-hull surface samples. The hull may bridge concavity
and leave sparse contact sampling; a visible surface intersection does not prove
collision with a movable tet vertex. Every non-root particle also has an attachment
constraint. The reported 0.0889 cm maximum residual does not establish whether poor
contact coverage, constraints, or rendering is the dominant cause.

## Required next measurements and repair order
1. Resolve and display actor world + full instance identity in the panel; log actual
   enabled transitions and post-release solver/deformer counts once per transition.
2. Capture CPU/Render/GPU timing for the same actor and pose: contact never enabled,
   enabled idle, pressing, disabled after pressing. Separately close debug panel.
3. Add timed scopes for initialization, collision gather, each solver, volume rule,
   output copying and GPU publication; avoid per-frame text logging.
4. Count movable vertices intersected by the press sphere, penetration before/after
   solving, and rendered residual. Stop treating "solver active" as contact success.
5. Fix deterministic duplicate work and idle scheduling; assess a single contact
   solve around the animated reference rather than two full solves. Do not discard
   the baseline without checking neutral shape and Jiggle composition.
6. Replace hull-only contact sampling with a budgeted boundary-conforming cage and
   consistent embedding if measurements confirm coverage failure. Do not merely
   increase pressure, lower Young's modulus, or uniformly subdivide everything.

## Primary online references
- https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-quickstart
  Vertex-based rigid collision, one-way interaction, low-resolution runtime guidance.
- https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview
  Low-resolution simulation with render surface deformation, skeletal constraints.
- https://dev.epicgames.com/documentation/unreal-engine/stat-commands-in-unreal-engine
  stat unit separates Frame/Game/Draw/GPU/RHIT.
- https://dev.epicgames.com/documentation/unreal-engine/introduction-to-performance-profiling-and-configuration-in-unreal-engine
  Profiling guidance and Unreal Insights.
- https://dev.epicgames.com/documentation/unreal-engine/ineditor-testing-play-and-simulate-in-unreal-engine
  Editor vs PIE/SIE lifecycle.
