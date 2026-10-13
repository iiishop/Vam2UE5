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
