# Leg runtime skinning requirements

The continuous leg helper field uses Unreal's native `MAX_TOTAL_INFLUENCES`
capacity (12 on the validated UE 5.8 desktop target). It is not an eight-slot
fallback: silently dropping leg participation at a saturated vertex is invalid.

Before generating or opening these new runtime bundles, enable this project
setting in `Config/DefaultEngine.ini`, then restart the editor:

```ini
[/Script/Engine.RendererSettings]
r.GPUSkin.UnlimitedBoneInfluences=True
```

The current SmartNPC project and isolated validation host have this setting.
The builder explicitly sets the LOD influence limit; native reconstruction and
quantized-weight checks remain mandatory. Existing eight-influence assets remain
usable. Other platforms must validate their skinning path before cooking.

Generate a new immutable runtime bundle after changing the leg builder. Rebuild
Corrective against the final weights and retain B1 final-data seals. Do not point
an old Corrective profile at a newly weighted mesh.

## Motion tuning (2026-10-14)

The default thigh/calf damping multipliers are 0.40 / 0.55. Reset uses the same
values. Amplitude remains 3 and support remains 0.85. These are conservative
visual calibration values, not measured physiological constants. Existing
per-character overrides remain editable.

For a linear isolated node, the effective damping ratio is approximately
`profile_zeta * damping_multiplier / sqrt(support_multiplier)` at unit mass
scale. Coupling, passive tension, limits and time integration change the observed
response; use recorded ring-down rather than this estimate for acceptance.

The running study at https://pmc.ncbi.nlm.nih.gov/articles/PMC8370632/ defines
power decay as `exp(-c*t)` and amplitude decay as `exp(-c*t/2)`. Its `c` has units
of inverse seconds and must not be copied into the dimensionless damping UI.
The current passive pose-tension approximation is not active muscle recruitment.
Research notes and same-condition recordings are in `Evidence/LegResearch`.
