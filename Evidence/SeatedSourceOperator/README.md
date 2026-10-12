# Source bulge initialization: verified defect

Actual original VaM runtime nodes apply inverse initial vertex-to-original-bone distance in metres to each bulge mask. Serialized IR retains unscaled masks. Prior mathematical source ports omitted this preparation.

`audit_vam_skin_operator.py` independently checks the prepared masks against captured runtime weights, then replays actual hip 30/60/90/110 matrices and pre-skin buffers with core drivers off/on. Maximum regional error against actual GPU raw positions is 0.00002504 cm. This is an isolated-axis mathematical verification, not Unreal visual acceptance.

The final UE character still has an LBS approximation and helper-weight redistribution. Restoring source Bend Fix/knee increments alone (candidate 8) does not remove the seated crease. It is not deployed as a visual fix. A separate opt-in source-operator reference experiment is in progress.

Capture directories are independent. Original VaM plugins were restored and hash checked. The first exact-camera attempt included UI/overexposure; use r2 gray runtime surface captures for geometric comparison. Render engines' lighting units and materials differ; equal light directions do not mean photometric equivalence.


## Actual UE opt-in experiments (2026-10-12)

Candidate 9 replaces the synthetic rotation-blend reference with the initialized source thigh-X response. Candidate 11 places this operator correction in the fixed source field and conditions independent shape axes jointly with blended-pose safety. The original edge/triangle predicates remain active. Both policies default to false.

Candidate 11 was built and independently reloaded as `ecce0c124fceb8f5487125ee0a8c13f7a0567986ecad27fba14fac9f8a7c3cd3`. All 14 existing joint regression tests passed. Standing, seated, knee-120 and abducted actual Editor/PIE layered surfaces are byte-numerically equivalent (maximum positional delta 0 cm). Contact and Jiggle are off for isolation; helper structure and corrective remain on.

At the exact VaM reference pose, lateral glute RMS is 2.113/2.551 cm (left/right), versus 2.942/3.414 cm before. This is NOT a visual PASS: the underside still has abnormal creases and the side view is unsatisfactory. No stable runtime configuration was replaced. Source initialization is a verified defect; the remaining crease mechanism is not yet fully resolved. Compound-pose source-reference extension is experimental, not a complete DAZSkinV2 implementation.

The normalized source fit also passes the unchanged Stage05 held-out mathematical gates, but no new source asset was imported from that fit in these experiments. Do not treat the fit metrics as the candidate runtime weights.
