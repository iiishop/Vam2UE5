# S2 capture diagnosis (supersedes the unresolved streaming hypothesis)

Both old c8a07713 and new 9936eed3 assets show the thigh ring with only 7 resident mips (64x64); both lose the conspicuous ring when the real capture camera is submitted as a regular streaming view, giving 9 resident mips (256x256). No UVs, texture pixels, normals, weights or materials were altered for the old-asset control.

Cause: offscreen SceneCapture does not supply a player streaming view. Our added view incorrectly set bOverrideLocation=true. UE ContentStreaming.cpp SetupViewInfos accepts those views only in its movie override branch; it filtered every capture view. InvestigateTexture confirmed DynamicReference exists, TexelSize=128.873, but Size=0 and Wanted=64. With bOverrideLocation=false, Size=318.149 and the ring disappears.

The old A2/B1/B2 visible ring is therefore not evidence of a geometry crack. 66 provenance-matched seam pairs have zero positions/weights/Morph mismatch and normalized bind-normal mismatch below 1.5e-6 degrees. Curvature/adjacent face diagnostics and full new-asset dynamic regression remain separate acceptance gates.

Generated material texture sampling metadata was also empty. The affine graph exporter now persists actual UV channel and scale without RHI; all 30 materials passed, including the engine default reference graph's constant division. Metadata alone did NOT cure the filtered-view capture: it is production streaming-data hardening, not the proven visible-ring cause. Unknown coordinate graphs fail explicitly; texture pixels and ordinary streaming stay unchanged.

Tests: UE C++ compiled; Vam.Material.AffineStreaming passed (affine offsets/scales/channel, division, rejection/atomicity); v4 B1 FinalCorrective passed; real Editor captures old/new completed. Source paths: VamPoseSuite.h, VamStage06AssetEditor.cpp, ue_stage06_materials.py, ue_runtime_build.py. Full evidence and original logs in Saved/LowerBodyContact/C1.5-S1-S2.

Final S1/S2 joint acceptance is still pending, not PASS.
