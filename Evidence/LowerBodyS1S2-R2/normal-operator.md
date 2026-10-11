# Native final-surface normal operator

Candidate-4 main-project normal BP, native recompute off/on: requests, cameras and actual bone matrices are identical; every A1 stage 00–04 has exactly zero position difference. The triangular lower-glute shading fold disappears under native Skin Cache recomputation. See normal-operator-ablation.json for source image hashes.

Generated final corrective meshes now enable bRecomputeTangent only on sections containing actual Glute/Leg influences and explicitly opt this LOD into Skin Cache. No global forcing cvar is installed. Original UV/material sections remain; world-normal and material seam regression is required.

Final corrective identity now covers section normal reconstruction flags, mask channel and LOD cache usage. The actual asset test mutates a flag, confirms identity rejection, restores it, and confirms valid state. All 14 requested native regressions pass with candidate-5 in the main project (main-regression/tests.log), including all 20 authored target replays. Editor/PIE visual regression is ongoing; this commit is not a standalone final acceptance claim.

Runtime requires the project's existing compute Skin Cache support. Active Contact Deformer is a separate operator and its historical rest-volume gate is tracked separately; no Contact safety gate was weakened.
