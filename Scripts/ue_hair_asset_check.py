"""Actual saved UAsset composition reuse and stale-cache rejection checks."""
import json
import os
import traceback
from pathlib import Path
import unreal as u

out=Path(os.environ['VAM_HAIR_ASSET_AUDIT'])
checks=[]
try:
    bundle=json.loads(Path(os.environ['VAM_HAIR_REPORT']).read_text())
    config=u.load_asset(bundle['configuration']);cache=config.get_editor_property('hair')
    style=config.get_editor_property('hairstyle');entries=style.get_editor_property('entries')
    assert style==cache.get_editor_property('hairstyle') and len(entries)==4
    assets=[e.get_editor_property('hair') for e in entries]
    assert len({a.get_path_name() for a in assets})==4
    for asset in assets:
        payload=json.loads(asset.get_editor_property('source_data_json'))
        assert set(payload)=={'schema','source','scalp'} and payload['source']['dynamic']['hair']
    checks.append('four distinct persisted source resources; character -> hairstyle -> resources')
    rebuilt=u.VamHairBuilder.build_hairstyle(os.environ['VAM_HAIR_INPUT'],'/Game/VamHairLibrary')
    reused=rebuilt[0] if isinstance(rebuilt,tuple) else rebuilt
    assert reused==style
    checks.append('same source catalog reuses existing asset objects')
    # Unreal property arrays/structs can be live views. Snapshot independent
    # structs before mutating the property, otherwise the restore mutates too.
    old=[]
    for entry in style.get_editor_property('entries'):
        copy=u.VamHairstyleEntry()
        for key in ('hair','enabled','appearance_overrides_json'):
            copy.set_editor_property(key,entry.get_editor_property(key))
        old.append(copy)
    try:
        style.set_editor_property('entries',list(old)[:1])
        assert not u.VamHairBuilder.bind_hairstyle(cache,config,style)
        checks.append('single-resource composition accepted as data; old four-resource render cache rejected')
        style.set_editor_property('entries',list(reversed(old)))
        assert not u.VamHairBuilder.bind_hairstyle(cache,config,style)
        checks.append('composition order change invalidates cache')
    finally:
        style.set_editor_property('entries',old)
    assert u.VamHairBuilder.bind_hairstyle(cache,config,style)
    checks.append('original composition restored and exact derived cache validates')
    out.write_text(json.dumps(dict(status='passed',checks=checks,hairstyle=style.get_path_name(),
                                  resources=[a.get_path_name() for a in assets]),indent=2))
except Exception:
    out.write_text(json.dumps(dict(status='failed',checks=checks,error=traceback.format_exc()),indent=2))
    raise
