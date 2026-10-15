# Remaining lower fold: independent defects

All weight-stage comparisons reuse candidate-2 A1 actual matrices and source-ID topology. They are numerical reconstructions, not substitute UE screenshots.

- Edge 11270–11273: Glute input 11.237°, after Leg allocation 11.237°, geodesic peak extension 41.516°, after moment fitting 22.141°.
- Peak extension uses one global maximum as every seed value; this overwrites local variation with a pelvis-held plateau. Preserve original support as an obstacle and core/band boundary as Dirichlet values instead. Only the additional field changes; no vertex-position smoothing and no left-to-right copy.
- Candidate 3 actual normal BP Editor capture: this edge is 13.686° at source LBS, .994° final-weight reference helpers, 4.132° static helpers, 2.251° corrective. Other explicit lower probes are retained in fold-candidate-3.json.
- Candidate 3 still shows a triangular shading feature. Native exported triangles versus authored bind normals: 61,748 opposite, 266 aligned. Corrective AddMorph formerly subtracted un-oriented cross-product normals, producing the opposite morph delta for UE clockwise topology. New VamMorphNormalUtils aligns the undeformed area normal to the authored normal and applies that same sign to the deformed area.
- Native tests Vam.Glute.MorphNormalOrientation, Vam.Glute.FinalInfluenceBudget, Vam.Leg.PreserveSupportSlots, Vam.Breast.WeightCompression all pass in candidate-4/unit.log. Full asset and Editor/PIE validation follows; this commit alone is not final S1/S2 acceptance.

Contact rest-volume failure stays historical and separate. Existing immutable reports are unchanged.
