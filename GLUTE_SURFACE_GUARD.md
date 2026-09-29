# Current correction: preserve common motion

The original whole-side guard below was rejected in manual observation: it suppressed nearly all visible jiggle. Runtime now measures and scales ONLY the regional residual after subtracting the mass-weighted common output. Final output is `Amplitude * Common + residualScale * regionalResidual`. Common motion is never attenuated by this guard; the user's Amplitude remains 3. Velocity arrows follow the same decomposition (still not an exact derivative of the output map).

The old whole-output gradient bound below no longer applies. The bound now covers only regional residual. Creases driven by common motion across a sharp waist weight transition remain possible; this correction restores amplitude, it does not establish a fold-free solution. Existing SurfaceGuardVersion=1 data are compatible because the stored weight derivatives are unchanged. Existing BP assets need no regeneration; restart the editor after installing updated DLLs. Diagnostics explicitly label residual gain and common motion 100%.

## Historical implementation and evidence (superseded output rule)

# Hip surface-gradient protection (2026-09-29)

## Finding and scope

The reported upper/lateral glute crease is consistent with a large displacement gradient where moving skin meets the waist. A screenshot does not establish a unique cause: imported shape, pose correctives, lighting, and the leg transition can also contribute. The previous common-mode output fixes upper/lower separation, but common bone displacement still becomes a nonuniform surface displacement through varying skin weights. Amplifying the result after the solver bypasses its displacement limits in the rendered output.

## Research

- Naturalis author documentation, Advanced Soft Physics: https://everlastervr.github.io/docs/naturalis/v1_3/advanced_soft_physics/ . Local Distortion Physics responds to neighbor compression/stretch; its pressure gradient reduces edge expansion to avoid sharp boundaries. This supports examining the skin deformation, not only joint travel. Public documentation was consulted, not proprietary implementation.
- Naturalis directional morphing: https://everlastervr.github.io/docs/naturalis/v1_3/directional_force_morphing/ . Separate shape correction accompanies motion; increasing displacement alone does not preserve form.
- Benchekroun et al., SIGGRAPH 2023, Fast Complementary Dynamics via Skinning Eigenmodes: https://www.dgp.toronto.edu/projects/fast_complementary_dynamics_site/ . Uses rig/material-aware skinning subspaces and reduced elastic simulation. We use the general lesson that skinning weights are part of deformation mechanics; this patch does NOT reproduce their eigenmode or elastic solver.

## Implementation

Generate UE5 Character Asset now builds surface data after both glute and leg weight redistribution, using the final native body reference mesh and helper weights. No character ID or fixed world-space location is involved. For every nondegenerate triangle touching either side's five helpers, store each helper's two tangent weight derivatives. Store these immutable data in DA_GluteJiggle, SurfaceGuardVersion=1; validate finite values, side membership, five-node shape and budget. The receipt and independent reload require the saved version.

Runtime first computes the existing mass-weighted common translation and regional residual, with Coherence and upper residual tether. After Amplitude, evaluate:

    Dx = sum(nodeOffset[n] * dw[n]/dx)
    Dy = sum(nodeOffset[n] * dw[n]/dy)
    g = max_triangle sqrt(dot(Dx,Dx) + dot(Dy,Dy))
    scale = (1 + (g / budget)^4)^(-1/4)

The same continuous scale multiplies all five output offsets on that side. Budget is 0.2 per side. The reference tangent gradient norm of the added displacement is therefore below that budget (both sides combined <= 0.4 by triangle inequality). This is a conservative displacement-gradient guard, not an exact strain, volume, or inversion solver. Upper is not pinned separately, preserving the previously requested whole-glute motion. Solver state is not shared or modified by the guard; only bone output is limited. Debug velocity arrows are scaled solver velocity, not the exact time derivative of the nonlinear output map.

No full CPU skinning, new procedural mesh, collision, source bone replacement, or additional independent physical node is introduced. Work per frame depends on the stored affected triangles and five nodes; a runtime performance benchmark has not been run.

## Usage and limitations

Use a newly generated/Upgrade Runtime BP. Old version-0 profiles remain compatible but explicitly request an upgrade in diagnostics. Glute Jiggle diagnostics report Surface guard version, left/right output gain and number of stored gradients. Gain below 1 means surface protection reduced motion. Existing Amplitude remains 3; increasing it cannot bypass the guard.

The metric is calibrated in imported/reference geometry. Large Shape edits, compressed poses, base animation and corrective deformation are not included in a current-pose strain calculation. No universal no-fold or volume-preservation guarantee is made. A sharp weight gradient can reduce the entire side's motion, and the current maximum across triangles is continuous but not differentiable when the worst triangle changes. A future posed-surface constrained optimization could preserve more motion while distributing correction locally. Visual comparison in jumping, sideways and prone poses remains user acceptance.

## Delivery evidence

- Baseline HEAD: b0adfe07b158c14e3c21d474a22ecf654d9a85c1, feature/jiggle/hip; changes remain uncommitted.
- UnrealEditor Win64 Development compile/link: succeeded.
- UnrealGame Win64 Development compile (`-gather`): succeeded; this is not a cook/package result.
- Installed five Editor binary/module files; source/destination SHA256 matched. Backup: `I:/Document/UE5/SmartNPC/Saved/HipSurfaceGuardBackup/20260929-223646`.
- Generated 3405 triangle/side gradient records for the primary character.
- New BP: `/Game/VamRuntime/R_cc58016dc3aa523d5a1f04c2/BP_VamCharacter`.
- Profile: `/Game/VamRuntime/R_cc58016dc3aa523d5a1f04c2/DA_GluteJiggle`.
- Publication evidence: `Saved/LegJiggleT1/PrimarySurfaceGuard/runtime-report.json` and build/reload/verify logs. Check report status for the final transaction result.
- No automation suite, frame-time benchmark, cook, or visual acceptance performed in this revision. Previous generated BP retained for comparison.

Final publication status: committed; all build/reload/verify processes returned 0.

## Final-weight boundary revision

Following the second manual rejection (upper step and lower fold), final hip skin participation now receives a topology diffusion pass AFTER leg redistribution and influence compression. The scalar field is the sum of actual hip helper weights on each side. Neighbor averaging uses inverse reference-edge length; a screened fidelity term grows toward the interior. The field can only decrease within existing support; removed mass returns proportionally to existing pelvis/thigh weights. Helper ratios, influence identities/counts, normalization, and neutral bind reconstruction are preserved algebraically. Source seam copies are welded again after the pass. No source mesh positions or morph deltas are changed.

This addresses a build-stage gap: smoothing region evidence before nonlinear participation mapping and influence compression is not equivalent to smoothing the final skin weights. It does not guarantee a crease-free mesh under arbitrary motion. In particular this iteration does not expand support outside the existing region, constrain posed strain, or couple hip/leg solvers dynamically. Interior weight changes are strongly penalized, not identically zero. Boundary movement may decrease locally, but the runtime whole-glute mode remains unscaled and Amplitude stays 3.

The new generated BP is required for this revision; DLL replacement alone does not change existing skin weights. Build publication evidence is under Saved/LegJiggleT1/PrimaryFinalBoundary. Editor compile/link succeeded. No automated test suite or visual acceptance was performed.

Final-weight revision publication committed: /Game/VamRuntime/R_8f46c1b2ff32ebcbe53995ca/BP_VamCharacter. Build/reload/verify returned 0. Left/right affected vertices 408/477; mean returned support weight 0.012942/0.009107. Editor binaries installed with matching SHA256; backup under Saved/HipBoundaryBackup/20260929-224601.
