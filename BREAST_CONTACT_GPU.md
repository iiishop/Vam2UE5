# Resident GPU runtime integration — 2026-10-09

Baseline HEAD: `4df4b3be19e639eb593365ff1a53f6bfa6c47771`. Changes uncommitted.

## Implemented

Final animated pose -> two-character RDG batch -> resident GPU buffer -> custom Optimus Data Interface -> original skeletal mesh. No per-frame synchronous readback. Static topology/adjacency is cached. Asynchronous diagnostics every 30 batches. Per-instance opt-in `BreastContact.bUseGPU`, debug checkbox, and `vam.Contact.GPU 1`; CPU remains default. Unsupported collision geometry/scaling and detected numerical failure fall back to CPU; Reset clears the failure latch. Contact Enabled still leaves Jiggle independent.

Automatic generation now emits CPU and GPU Deformer graphs and includes GPU sources/shaders in build identity. Profile stores GPUSurfaceDeformer. No shared skeleton mutation. Runtime outputs go directly into the existing morph/skinning/normal deformer graph.

## Real two-character rendered viewport results

|Phase|CPU ms / FPS|GPU run 1 ms / FPS|GPU run 2 ms / FPS|
|---|---|---|---|
|Held pressure|74.681 / 13.4|9.175 / 109.0|9.253 / 108.1|
|Moving pressure|73.159 / 13.7|9.169 / 109.1|9.296 / 107.6|

GPU P95 10.17–10.42 ms. RTX 4070 Ti, i5-12600KF, UE5.8.3, DX12 1920x1080, sg=2, vsync off. Normal -game loop with offscreen GPU rendering, not kernel timing; excludes display Present. Entry scene, two identical profiles, one pressed breast each, 300 measured frames/phase; two GPU repetitions. Not a complex game with clothing/hair physics or Shipping. Final runs did not trigger fallback. Actual render readbacks contain 33,981 vertices per character.

## Numerical model and limitations

Experimental edge/volume projection, NOT an equivalent Chaos corotated material port. 96 main Jacobi iterations, 128 alternating surface/volume barriers and 64 final feasibility sweeps. Per-side volume reduction, contact tangent gradients, nipple-weighted surface edge limits. The final feasibility sweeps resolve sampled inverted cells that disqualified earlier faster candidates.

Final sampled volume error rounds to 0.00%, no sampled inversion. Local shape differs: GPU moving example surface edge ratios 0.617–2.299, nipple RMS strain 0.121; CPU held example 0.828–1.393, 0.041. Different pose instants, not matched-pose equivalence. Vertex sphere contact only; no triangle contact, soft-soft contact or reaction force port. Diagnostics sample asynchronously; not proof of every-frame validity. No visual acceptance, calibrated material equivalence, packaged Game or cook validation claimed.

## Isolated assets / use

Drag `/Game/VamRuntime/GPUContact_20261009_v2/BP_VamCharacter_GPU` into an ordinary level. GPU opt-in is true on this candidate only. Sibling assets: `RC_Runtime`, `DA_BreastContact`, `DG_BreastContact_GPU`. Source character assets are unchanged; candidate RC reuses the old identity/receipt and is experimental, not a new committed runtime identity.

Debug panel -> Breast Chaos Contact -> GPU Contact; Enabled toggles all contact independently. Programmatic mode change: set bUseGPU then ResetContact. Unsupported geometry falls back to CPU.

## Evidence

`Saved/ContactGPURuntime/compare.html`, `summary.json`, `cpu-reference`, `feasible1`, `feasible2`: frame CSVs, commands, source hashes, shader snapshot, screenshots, surface readback, diagnostics. Editor compilation succeeded. Candidate saved; installed modules backed up and hash-checked. Main-project reload logged separately. No old assets deleted.

Reproduce: `Scripts/run_contact_gpu_runtime.py gpu96 <fresh-name> 128`, or `cpu` for native baseline. Runner rejects observed fallback and missing rendered vertices. `create_gpu_contact_candidate.py` refuses overwrites. Historical kernel tests below were not rerun against the new runtime schedule and do not validate this integration.

---

## Historical kernel experiment (not runtime FPS)

# GPU batch contact experiment — 2026-10-09

Baseline: `4df4b3b`. Experimental implementation; production CPU contact is unchanged.

## Implemented

`VamContactGPU` PostConfigInit module, UE global compute shader/RDG, two-instance batched buffers, tetrahedron edge and signed-volume projection, kinematic roots and per-instance spherical contact. Two schedules compared: 54-color in-place projection and two-pass Jacobi/gather. 2,760 particles / 10,674 tetrahedra; nominal structured-buffer allocation 1,146,176 bytes (excludes driver/RDG overhead). Uses original contact tetrahedral assets, without asset migration.

## Actual second-run DX12 measurements (RTX 4070 Ti)

|Jacobi iterations|Min ms|Median ms|Max ms|Max side volume error|Min J|Inversions|
|---|---|---|---|---|---|---|
|12|1.131|1.147|1.444|0.243%|0.321|0|
|24|1.410|1.531|2.123|0.342%|0.588|0|
|48|1.934|2.174|2.501|0.412%|0.660|0|
|96|2.651|2.660|3.451|0.439%|0.684|0|

Time is synchronized RDG submission/upload/dispatch/final readback wall time, after an initial GPU idle. CPU topology/color/adjacency construction is excluded. Three repetitions only; not statistically robust frame-time percentiles. This is neither a pure GPU timestamp nor full-character/game FPS. Two translated copies of the same profile are used, with one pressed breast per character; not four pressed breasts or mutual collision.

Colored projection produced six inverted tetrahedra at every tested iteration count; rejected. Jacobi passed finite output, roots <0.001cm drift, <1% volume error and <0.001cm particle penetration on this fixture. Added neutral and unpressed second-instance isolation checks pass. Surface triangle penetration is not measured. Volume error increasing slightly with iterations shows approximate competing projections; this is not an exact incompressibility solver.

## Material/quality limitation

This prototype uses six edge-length projections and one signed-volume projection per tet. It is NOT the existing Chaos corotated GS material port, has no equivalent calibrated Young/Poisson response, and must not be described as preserving current visual quality. No interactive animation, release history, nipple-region material differentiation, surface triangle collision, mutual soft contacts or rigid reaction force validation. No screenshot/visual acceptance was performed. Test success does not approve the rejected colored candidate.

## Runtime limitation and next integration

The experiment intentionally reads back once after the entire batch and blocks. Never call this API per frame as a production optimization. Production characters still run the existing CPU backend. Next: persistent GPU buffers and precomputed topology, final-pose input, direct GPU output binding into native skeletal deformer without CPU readback, calibrated material response and pressure/release/shape comparison against CPU. Then run the same two-character viewport benchmark including render cost. 60 FPS is not established by these numbers.

## Reproduce

Build host UnrealEditor with the new module/shaders, then run `Scripts/run_contact_gpu.py`. Test: `Vam.Breast.GPUContact`. Explicit fixture: `/Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime`. Evidence: `Saved/ContactGPU/{command.json,runtime.log,test.json,timings.csv,summary.json,positions-*.bin}`. Runner requires automation Success and no reported test errors.

References used for UE implementation: [RDG](https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine), [global shader plugin](https://dev.epicgames.com/documentation/unreal-engine/creating-a-new-global-shader-as-a-plugin-in-unreal-engine). No external solver code copied.
