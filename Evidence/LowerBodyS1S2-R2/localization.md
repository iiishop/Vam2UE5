# R2: source-provenance fold localization

Baseline: normal generated BP, identity 0db00772270507ebce3eb68d3be1ae2b25d58ceb95252eca3adbd6ebcf88e921; previous bbfc69f. Historical evidence untouched.

Source edge 19403–19495: source LBS 4.1415°, final weights/reference helpers 4.6359°, static helpers 68.4398°, corrective 82.6941°. Edge 11221–19501: 0.0504° / 0.1256° / 54.2474° / 68.6523°. These prove that the extra sharp fold starts at static helper deformation, before corrective; corrective further amplifies it. The 00 baseline is actual source LBS, not disabled dynamic switches.

Final weight samples reveal the build-time trigger: source 19403 retains R_Glute_Medial=0.25931, but neighboring source 11208 omits that helper while preserving abdomen2=0.00026. Glute Structure's legacy Redistribute reserves all source slots before choosing helpers. Thus a tiny original influence can remove a significant helper. Leg's already-fixed retention cannot restore a helper missing in the Glute input.

Four real Editor ablations are in Saved/LowerBodyContact/C1.5-S1-S2-R2/isolation. `weights-only` means final weights with reference helpers, NOT original source skinning. Disabling layers is diagnostic only. All four completed successfully; production remains enabled. Lower infragluteal forms must still be compared with source geometry, separately from the quantified lateral sharp edges.

Candidate correction allocates the eight-slot budget from the entire final weight candidate. It is restricted to Glute Structure and does not alter Breast's non-donor-preservation contract. Actual build, new corrective bake, and validation are in progress. No PASS claim at this checkpoint.
