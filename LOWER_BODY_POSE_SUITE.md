# A2: Lower-body pose and capture suite

## Run

```
python Scripts/run_lower_pose_suite.py --project <HostProject.uproject>
```

Use `--config`, `--blueprint`, `--editor`, `--output` to select the actual runtime bundle and isolated UE host. `--case suspended-seat` replays one case. Output directories must be new. The runner saves the exact command; recipes live in `Config/LowerBodyPoseSuite.json`.

18 representative cases, not a Cartesian product: hip 0/30/60/90/110; knee 0/30/60/90/120; suspended sitting, seat contact, deeper flexion, one-leg and asymmetric poses, abduction and opposite rotations, side-lying/prone orientation of the same seated local pose.

Requested anatomical directions are converted into local quaternions using the actual shape-adjusted skeleton. Each quaternion is checked against the loaded RigProfile's local limits. Out-of-range requests are reduced along the same rotation path and labelled; requested degrees never substitute for actual transforms. The geometric debug path skips the redundant final Euler clamp only after this validation.

## Capture contract

- Existing static Glute Structure and Corrective remain active; Breast/Glute/Leg Jiggle, breathing, idle, blink and body recoil are disabled. Only the seat-contact case requests GPU contact.
- Clear contact, Jiggle and body response history between cases. Fixed 1/60 second step, 90 settling frames; seat rises 4 cm over 60 frames with smoothstep. Seat dimensions/transform and solver status are saved.
- Freeze the character for all views/modes. Side/rear/front/oblique; original materials, untextured gray, white silhouette on black (seat hidden only in silhouette), and wireframe. 768 square, 42-degree FOV, manual exposure bias 2. Temporal AA and motion blur disabled.
- Camera rig rotates with the actor for orientation probes; world gravity does not. Compare component-space surfaces to distinguish posture changes from gravity. These no-Jiggle captures verify static orientation invariance, not gravity dynamics.
- Every state retains actual component/local bone matrices, body-to-world, world gravity in body coordinates, all camera matrices, limit results, and the A1 diagnostic bundle (shape, identity, weights, mapping and surfaces).
- Existing invalid cage/solver states are labelled `blocked_contact_solver_inactive`, never accepted as functioning contact deformation. `captured_with_limit_reduction` is not proof that the requested angle was reached.

## Review / local metrics

`report_lower_pose_suite.py <output>` writes `compare.html`, image SHA256 hashes and `metrics.json`. Hip/knee/calf neighborhoods retain local bounds, maximum/P95 stage displacement and the eight worst render vertex indices. The neighborhood radii are explicit diagnostic ROIs, not anatomical tissue masks. Images retain the full silhouette, so a whole-body RMS cannot conceal a local fold.

`replay_deformation_audit.py <audit_bundle>` reconstructs frozen geometry independently of solver history. Re-run a case for the UE material rendering; compare the actual matrices and local extrema before comparing image pixels.

## VaM reference

`compare_pose_transforms.py <state.json> <converted-vam.json>` accepts only `space: ue_component_cm_row_major` and `actual_bones: [{bone, component_matrix: [16 row-major numbers]}]`. Convert VaM axes, units, bind conventions and bone names first. It reports translation, rotation and matrix errors per supplied bone; raw UI Euler labels are rejected. No VaM reference data is fabricated or claimed to have been validated by this suite.

The runner enables UE Skin Cache shader support in the isolated host (matching the main project) and forces the capture body to use Skin Cache. This avoids an engine morph double-buffer assertion when switching materials on a frozen skeletal component. Material permutations are explicitly compiled for the world feature level before capture. These are capture-tool settings, not physics tuning.

`compare_pose_suite_replay.py <first-run> <second-run> --output <json>` compares actual bone matrices, pre-contact surfaces and cameras. Recipe copies and source hashes are kept in each run; silhouette pixel row bounds use the recorded fixed capture settings.

## Recorded run (2026-10-09)

`Saved/LowerBodyContact/A2-complete/compare.html`: 18 cases / 288 PNGs. Independent suspended-seat replay produced identical component bone matrices and pre-contact surfaces; camera maximum element difference 7.11e-15. Sideways/prone probes produced identical component-space surfaces. Seat-contact is explicitly blocked: tet 5592 signed volume -0.00121148629 cm3, active solvers 0. This is a reproducible failed contact sample, not completed contact-physics acceptance. VaM transformed-bone reference remains unavailable.
