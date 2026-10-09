# Breast contact Stage 3 — coupled GPU contact and body response

Baseline: `48795bd`, `feature/chaos/breasts`. Working tree implementation; no source assets migrated and no commit made by this task.

## Runtime contract

The existing GPU corotated material, volume safeguards, skin bending and render binding remain. This adds a scene-level contact layer to that backend; it is **not** a claim that Epic's stock Chaos Flesh now has a GPU two-way solver.

- Registered GPU volumes collide across regions of one character and across characters in the same UWorld. Vertex/triangle contact distributes corrections by inverse mass and barycentric weights; per-cell backtracking retains positive tetrahedra.
- Sphere, oriented box, capsule and convex simple collision shapes are read from UE collision components. Skeletal collision shapes use their individual bone transforms. Triggers are excluded. A character's articulated distal bodies can press its own soft region.
- Legacy rigid proxies for source soft-region bones are excluded while that region is represented by a volume. The exclusion comes from profile skin donor evidence and the rig's articulated semantics, not character names.
- Material-gradient reaction forces and moments are calculated on the GPU, reduced in parallel, and read asynchronously. No synchronous GPU readback or fence is added to ordinary runtime. Deleted colliders, stale packets and teleports invalidate loads.
- Simulating rigid bodies receive equal-and-opposite force and torque in UE units. Static/kinematic objects do not start moving merely because they were touched.
- `UVamBodyContactResponseComponent` consumes the soft load and produces a bounded, critically damped chest offset/rotation over animation. The transform is composed before IK and joint limits; descendants follow. A simulated chest receives the rigid force instead. Instance state is private.
- Pose slew limits avoid sudden feedback into Jiggle: default 20 cm/s translation and 60 degrees/s rotation. These limits apply to the new additive response, not Jiggle amplitude. Default offset/angle bounds are 6 cm / 20 degrees.

## Soft disabled: what happens when walking into an object?

`BreastContact.SetContactEnabled(false)` stops volumetric contact and restores the normal skinning/deformer path. It does **not** disable UE rigid physics or the independent body-response component.

With `BodyContactResponse.bEnabled` and `bRigidProxyContact` enabled, the existing chest capsule is swept against blocking simple scene geometry. That proxy can produce an upper-body recoil and push a simulating object, without a soft indentation. Disable Body response too to remove this added recoil.

This actor is an `AActor`, not a CharacterMovement locomotion implementation. The feature does **not** itself stop gameplay root motion, prevent the actor walking through a wall, or turn the entire skeleton into a ragdoll. Root blocking remains the movement controller's responsibility. The proxy covers the semantic chest, not every body part.

## Controls and API

VaM character debug panel → Breast Jiggle Runtime → Breast Chaos Contact:

- Enabled: volumetric contact only; Blueprint `SetContactEnabled`.
- GPU backend: required for this coupled path; now preferred when the profile contains a GPU deformer.
- Soft pairs: own-region and inter-character GPU contact.
- Force feedback: GPU contact loads to bodies / scene rigid objects.
- Body response: independent force-driven animation and chest proxy.
- Diagnostics include contact force in N, body offset, GPU pair-slot capacity, CPU scene preparation time and asynchronous active/search round counts.

Blueprint component properties expose the independent enable/proxy flags and response bounds. `AddContactForce(ForceNewtons, WorldPointCm)` and `AddContactWrench(ForceNewtons, TorqueNewtonMeters, WorldOriginCm)` allow gameplay forces through the same additive layer. `ResetResponse` clears its history. Shape and teleport revisions reset it automatically.

Existing `/Game/VamRuntime/GPUContact_20261009_v2/BP_VamCharacter_GPU` and `RC_Runtime` are used; inherited native components are added on load. No new character-specific configuration or test-level initialization is required. Collision sources need supported simple collision and a Block response to the body channel; enable Simulate Physics on a movable rigid object to observe reciprocal movement.

## Contact limits

This is a discrete quasistatic contact model. It has no swept soft CCD, friction or impact restitution. Deep initial interpenetration beyond the 3 cm neighborhood is unsupported; approach gradually. Edge/edge-only contact and very thin geometry are not guaranteed. The current GPU candidate search and conditional solve supersede the original CPU pair cache; see `BREAST_CONTACT_GPU_BROADPHASE.md` for implementation and updated timing evidence.

Reaction loads are a corotated-plus-foundation material-gradient estimate; they do not yet include the complete hard-volume and skin-constraint multipliers. Do not interpret them as measured tissue stiffness or fully calibrated contact stress. Additive animation is an actuated response, not an unconstrained momentum-conserving whole-body simulation.

Supported soft partners are volumes registered with this GPU backend. Arbitrary external cloth, other engines' soft bodies and native CPU Flesh are not automatically registered. Native CPU fallback remains available but is not feature-equivalent for coupled contact/force feedback. Instanced static meshes, complex triangle-mesh-only colliders, negative/nonuniform component scale and unsupported aggregate shapes are outside this GPU collision path. Own kinematic hand contact deforms the volume but does not synthesize a joint-specific arm recoil.

## Validation

Engineering evidence and remaining limitations are recorded under `Saved/ContactStage3`; final run results are appended below. Tests measure deformation, finite/positive cells, equal-and-opposite loads, actual rigid acceleration and additive skeletal response. They do not evaluate visual or anatomical realism.


### Recorded results

- `coupling-final`: GPU sphere/box/capsule/convex, mirror neutrality, different-world isolation, same-character internal wrench cancellation — Success.
- `interaction-final`: actual two-character controlled contact, rigid body reaction and independent soft-off proxy — Success. Real UE frames saved; fixed roots and body response disabled during pair geometry comparison.
- `installed`: rerun through **SmartNPC.uproject** after installing three matching DLLs — Success. Pair residual 0.82057 / 0.959526 cm, steady pair force sum zero; rigid press peak 16.1228 N, additive offset peak 1.41684 cm, rigid outward speed 12.7224 cm/s; soft-off proxy peak 98.7486 N. These are engineering fixture values, not tissue measurements.
- `Saved/ContactSkinResearch/stage3-final`: original glass trajectory — Success; left volume error −0.04%, min J 0.3412, no inversions, nipple RMS strain 0.05053, sampled render penetration 0 cm.
- Editor compile and explicit UnrealGame **plugin module compilation** succeeded. The latter is not a separately packaged executable playtest. The editor-only geometry-readback benchmark is now correctly guarded out of a Game build.
- `Saved/ContactGPURuntime/stage3-chestframe`: actual two-BP game viewport, 1920×1080 Quality 2, 300 measured frames per phase. Held 20.373 ms / 49.08 FPS, moving 20.441 ms / 48.92 FPS; P95 21.469 / 21.573 ms. Both GPU resident. This is two independent glass presses with soft-pair checks enabled, **not mutual-contact gameplay FPS**. Additional Stage 3 work has not retained the previous 60 FPS target; no claim of performance completion is made.
- Pair cache operates in a common rigid chest frame to avoid invalidation from whole-chest motion; relative deformation still invalidates it. No render or tetrahedral refinement or material weakening was used for performance.

Installed files and SHA-256 values are in `Saved/ContactStage3/install.json`; previous DLLs/manifest are preserved in `install-backup`. No source character assets were overwritten. Baseline HEAD unchanged; source remains reviewable working-tree changes.


Targeted Windows incremental Cook of `GPUContact_20261009_v2` and dependencies succeeded with 0 errors / 0 warnings (`Saved/ContactStage3/cook-final.log`). The first attempt used the physical target path of a Content junction, which did not resolve those packages; it is not counted. The successful run used the host project's mounted Content path. No packaged executable playtest was performed.

Contact wake bounds include the final helper translations and rotations, so Jiggle can bring two regions into contact even when their standing-pose boxes were separated. This affects candidate wake-up only; no additional Jiggle degrees of freedom or solver forces were added.

Final wake-bound regression (`stage3-wake`): both GPU resident, held 20.347 ms / 49.15 FPS, moving 20.351 ms / 49.14 FPS; idle is still zero solvers. Game plugin recompile after the wake-bound change succeeded (`game-build-wake.log`).

`installed-wake`: final installed SmartNPC integration rerun Success, including actual two-character contact, dynamic rigid reaction and soft-off body proxy. Hashes of all tested source files are in that run directory. No commit made.

## GPU candidate-search optimization (2026-10-09)

The earlier timings and CPU-cache notes above are historical Stage 3 evidence. The installed runtime now uses GPU BVH queries, indirect conditional repairs and per-pass shader permutations. See `BREAST_CONTACT_GPU_BROADPHASE.md` and `Saved/ContactStage3GPU/compare.html`. Actual project averages are 65–68 FPS for two independent presses, with remaining slow frames; the independent host measures about 75 FPS. Do not conflate these environments or claim stable 60 FPS.
