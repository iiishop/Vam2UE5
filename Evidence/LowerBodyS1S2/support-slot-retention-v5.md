# Preserve static helper semantics within the eight-influence limit

BlendLegWeights previously pooled hip helper mass, selected a subset of helpers along with leg helpers, then redistributed the entire hip mass across the selected subset. Static helper transforms differ; preserving only their sum does not preserve the authored posed surface.

Now the existing helper weights remain untouched. Leg donor mass uses only unused slots. A fully occupied vertex is not destructively repacked; existing Glute Jiggle still covers its glute helper influences. This does not disable the Leg system or change its global strength. Deterministic weight sorting remains for the new leg influences.

Actual old/new post-weld native build data: 533 rendered inputs / 475 logical source vertices differ in hip helper weights, maximum individual change 0.108033873. The initial 701-vertex 00-to-01 count included source-ID weld corrections and must not be labeled purely as slot truncation. Build enforces unchanged canonical hip weights immediately after leg transfer; transition retains the prior per-helper floors from 9cc1a15.

Tests Vam.Leg.PreserveSupportSlots, Vam.Material.AffineStreaming and Vam.Breast.WeightCompression passed. UE compilation and fresh runtime build/reload/verify passed. New asset identity 0db00772270507ebce3eb68d3be1ae2b25d58ceb95252eca3adbd6ebcf88e921. B1 and full Editor/PIE visual regression are being run independently; this commit does not declare joint PASS.
