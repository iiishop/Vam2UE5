# Glute shape retention after the continuity repair

Candidate 15 preserves more of the pre-continuity corrective field without restoring its sharp posterior edges. This is an incremental shape-retention candidate, **not a claim that both sides now match the user's preferred old left silhouette**.

- Production builder accepts `glute_corrective.continuity_edge_budget` in `(0, 0.5]`; default remains 0.25 for compatibility. Candidate uses 0.5 with `continuous_safety` enabled.
- Dijkstra lower envelope still only decreases the safety field, follows actual topology and proven aliases, and runs inside the existing axis/combined geometric safety loop. No safety checks removed. No preset vertex IDs in production code.
- Static helper and final-weight stage positions are unchanged (max 0 cm); exact pose, cameras, light, actor transform equality checked.
- Old-shape regional RMS: left 1.304 -> 0.752 cm, right 1.184 -> 0.814 cm. Right old geometry includes the defect; this metric measures retention, not aesthetic acceptance.
- Frozen 676 posterior edges: max 28.892 degrees, zero above 45 degrees (previous safe version max 23.532, zero above 45; old creased version max 91.803, 23 above 45).
- Independent runtime rebuild/reload confirmed; normal BP asset identity and source paths recorded in provenance.json.
- Main project stable binaries/configuration are not overwritten. HostProject shares generated Content and uses newly built modules. Explicit family policy remains opt-in.
- Contact historical cage failures are outside this static shape task and are not marked fixed.

Local HTML and real UE screenshots: `Saved/LowerBodyContact/GluteShapeRecovery/report.html`.

Evidence includes the actual metrics, topology-edge rows, same-pose VaM checks, family, recipe, test outcomes and hashes. Existing source-ID mapping is used, never nearest-neighbor correspondence. Larger continuity budgets were rejected by the offline screen because folds returned; sweep.json is an approximation and is not presented as UE validation.
