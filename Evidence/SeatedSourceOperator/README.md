# Source bulge initialization: verified defect

Actual original VaM runtime nodes apply inverse initial vertex-to-original-bone distance in metres to each bulge mask. Serialized IR retains unscaled masks. Prior mathematical source ports omitted this preparation.

`audit_vam_skin_operator.py` independently checks the prepared masks against captured runtime weights, then replays actual hip 30/60/90/110 matrices and pre-skin buffers with core drivers off/on. Maximum regional error against actual GPU raw positions is 0.00002504 cm. This is an isolated-axis mathematical verification, not Unreal visual acceptance.

The final UE character still has an LBS approximation and helper-weight redistribution. Restoring source Bend Fix/knee increments alone (candidate 8) does not remove the seated crease. It is not deployed as a visual fix. A separate opt-in source-operator reference experiment is in progress.

Capture directories are independent. Original VaM plugins were restored and hash checked. The first exact-camera attempt included UI/overexposure; use r2 gray runtime surface captures for geometric comparison. Render engines' lighting units and materials differ; equal light directions do not mean photometric equivalence.
