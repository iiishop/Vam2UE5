# R2: source-provenance fold localization

Baseline: normal generated BP, identity 0db00772270507ebce3eb68d3be1ae2b25d58ceb95252eca3adbd6ebcf88e921; previous bbfc69f. Historical evidence untouched.

Source edge 19403–19495: source LBS 4.1415°, final weights/reference helpers 4.6359°, static helpers 68.4398°, corrective 82.6941°. Edge 11221–19501: 0.0504° / 0.1256° / 54.2474° / 68.6523°. These prove that the extra sharp fold starts at static helper deformation, before corrective; corrective further amplifies it. The 00 baseline is actual source LBS, not disabled dynamic switches.

Final weight samples reveal the build-time trigger: source 19403 retains R_Glute_Medial=0.25931, but neighboring source 11208 omits that helper while preserving abdomen2=0.00026. Glute Structure's legacy Redistribute reserves all source slots before choosing helpers. Thus a tiny original influence can remove a significant helper. Leg's already-fixed retention cannot restore a helper missing in the Glute input.

Four real Editor ablations are in Saved/LowerBodyContact/C1.5-S1-S2-R2/isolation. `weights-only` means final weights with reference helpers, NOT original source skinning. Disabling layers is diagnostic only. All four completed successfully; production remains enabled. Lower infragluteal forms must still be compared with source geometry, separately from the quantified lateral sharp edges.

Candidate correction allocates the eight-slot budget from the entire final weight candidate. It is restricted to Glute Structure and does not alter Breast's non-donor-preservation contract. Actual build, new corrective bake, and validation are in progress. No PASS claim at this checkpoint.

## Candidate-1 failed; dense fitting correction

Allocating only the initial Glute budget from final candidate weights moved the discontinuity: source edge 11221–19501 reached 98.56° after corrective. This is preserved as a failed experiment. It proves early re-ranking alone is insufficient.

The full chain is: initial helper truncation -> Leg transition treats existing helper set as the only legal support -> 96-pass moment fitting amplifies surviving tiny weights while missing helpers remain exactly zero -> static transforms expose a crease. Example 19403 Medial: 0.0013275 -> 0.0022975 -> 0.2593121.

Candidate-2 preserves authored support floors, allocates added support over all five measured regional kernels, runs the existing moment fit with the complete candidate, and only then exports the eight-influence budget. No surface smoothing, forced mirroring, disabled corrective, or disabled glute support. Maximum discarded final candidate mass in this build: 0.009109636.

Compiled successfully. Vam.Glute.FinalInfluenceBudget, Vam.Breast.WeightCompression and Vam.Leg.PreserveSupportSlots all succeeded. Full asset generation and visual/regression verification are pending at this commit.
