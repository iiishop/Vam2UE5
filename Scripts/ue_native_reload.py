"""Independent editor-process reload gate before ImportState publication."""
import hashlib
import json
import os
from pathlib import Path
import unreal as u

data=Path(__file__).resolve().parents[1]/'Saved'
report_path=Path(os.environ.get('VAM_NATIVE_REPORT_FILE',str(data/'NativeBuild/latest-native-assets.json')))
report=json.loads(report_path.read_text(encoding='utf8'))
assets=[u.load_asset(p) for p in report['assets']]
assert all(assets),'An asset did not survive independent reload'
definition=u.load_asset(report['definition']);body=definition.get_editor_property('body').load_synchronous() if hasattr(definition.get_editor_property('body'),'load_synchronous') else u.load_asset(report['folder']+'/SK_Body')
assert body and len(body.get_editor_property('morph_targets'))>=1
assert len(u.VamNativeBuilder.get_render_to_input_map(body))>=1
skeleton=body.get_editor_property('skeleton');assert skeleton
shape=u.load_asset(report['folder']+'/SD_Shape')
preset=u.load_asset(report['folder']+'/AP_Imported')
geometry=u.load_asset(report['folder']+'/GD_Bindings')
assert shape and preset and geometry,'Runtime shape dependency missing after reload'
parameters=definition.get_editor_property('parameters')
assert len(parameters)>=1 and len(shape.get_editor_property('neutral_local_bind'))>=1
assert len(shape.get_editor_property('morph_set_lock_digest'))==64
assert len(geometry.get_editor_property('render_to_input'))==len(u.VamNativeBuilder.get_render_to_input_map(body))
assert dict(preset.get_editor_property('parameters'))=={p.get_editor_property('target'):p.get_editor_property('default_value') for p in parameters}
assert json.loads(geometry.get_editor_property('cloth_geometry_data_json'))['schema']=='vam-cloth-geometry/1'
assert json.loads(geometry.get_editor_property('hair_source_data_json'))['schema']=='vam-hair-source/1'
assert json.loads(shape.get_editor_property('formula_diagnostics_json'))
for asset in assets:
    if isinstance(asset,u.SkeletalMesh):assert asset.get_editor_property('skeleton')==skeleton,'Part skeleton diverged after reload'
    if isinstance(asset,u.Material):
        assert asset.get_editor_property('used_with_skeletal_mesh') and asset.get_editor_property('used_with_morph_targets'),'Persist both skeletal and Morph material permutations'
fingerprints={}
for path in report['assets']:
    package=path.split('.')[0]
    filename=Path(u.Paths.project_content_dir())/(package[len('/Game/'):]+'.uasset')
    assert filename.is_file(),str(filename)
    with filename.open('rb') as f:fingerprints[package]=hashlib.file_digest(f,'sha256').hexdigest()
report.update(independent_reload_verified=True,asset_file_sha256=fingerprints)
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
# A versioned destination never overwrites user-owned derivatives or changes shared binds.
plan=json.loads((data/'Plans'/(report['source_identity']+'.json')).read_text(encoding='utf8'))
state_dir=data/'ImportState';state_dir.mkdir(exist_ok=True);state_path=state_dir/'manifest.json'
state=json.loads(state_path.read_text(encoding='utf8')) if state_path.exists() else {'schema':1,'assets':{}}
assert state['schema']==1 and isinstance(state['assets'],dict)
for root in plan['roots']:
    source=next(i for i in plan['items'] if i['id']==root)
    state['assets'][root]={'sha256':source['sha256'],'destination':report['definition'],
        'source_reference_status':'partial','native_assets':report['assets'],'independent_reload_verified':True,
        'missing_parts':report.get('missing_parts',[]),'asset_file_sha256':fingerprints}
temporary=state_path.with_suffix('.tmp');temporary.write_text(json.dumps(state,ensure_ascii=False,indent=2),encoding='utf8');temporary.replace(state_path)
report['manifest_updated']=True
report_path.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
index=json.loads((data/'NativeBuild/native-destinations.json').read_text(encoding='utf8'))
for destination in index.values():
    if destination['folder']==report['folder']:
        marker=data/'NativeBuild'/(destination['commit_id']+'.commit.json')
        temporary=marker.with_suffix('.tmp')
        temporary.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
        temporary.replace(marker)
latest=data/'NativeBuild/latest-native-assets.json'
if report_path!=latest and latest.exists():
    current=json.loads(latest.read_text(encoding='utf8'))
    if current['folder']==report['folder']:
        latest.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
u.log('VAM_NATIVE_RELOAD_OK '+report['folder'])
