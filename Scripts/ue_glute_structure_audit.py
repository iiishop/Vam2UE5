"""G0 engineering audit and ordinary BP cook fixture. No visual acceptance."""
import json, os, sys, math
from pathlib import Path
import unreal as u
sys.path.insert(0, str(Path(__file__).resolve().parent))
from vam_runtime_recipe import verify_native_binding
report_path=Path(os.environ['VAM_RUNTIME_REPORT'])
report=json.loads(report_path.read_text(encoding='utf8'))
assert report['status']=='committed'
config=u.load_asset(report['configuration'])
definition=config.get_editor_property('definition')
profile=config.get_editor_property('glute_structure')
assert not u.VamGluteStructureBuilder.validate(definition,profile)
assert not u.VamBreastJiggleBuilder.validate(definition,config.get_editor_property('breast_jiggle'))
receipt=json.loads(config.get_editor_property('receipt_json'))
source=u.load_asset(receipt['source_definition'])
original=json.loads(u.VamStage06AssetEditor.describe_mesh_binding(source.get_editor_property('body')))
derived=json.loads(u.VamStage06AssetEditor.describe_mesh_binding(definition.get_editor_property('body')))
verify_native_binding(derived[:len(original)],original)
assert len(derived)==len(original)+24
assert len(definition.get_editor_property('parameters'))==len(source.get_editor_property('parameters'))
def v(x):return [x.x,x.y,x.z]
sides=[]
for side in profile.get_editor_property('sides'):
    weights=list(side.get_editor_property('region_weights'))
    assert all(math.isfinite(w) and 0<=w<=1 for w in weights) and max(weights)>.01
    nodes=[]
    for node in side.get_editor_property('regions'):
        row={k:node.get_editor_property(k) for k in ('bone_index','pelvis_attachment','thigh_attachment','effective_volume_cm3','mass_fraction_candidate','lever_arm_cm','support_baseline')}
        row.update(semantic=str(node.get_editor_property('semantic')))
        row.update({k:v(node.get_editor_property(k)) for k in ('rest','pelvis_point','thigh_point_local','mass_center','inertia_candidate')})
        assert abs(row['pelvis_attachment']+row['thigh_attachment']-1)<1e-8
        nodes.append(row)
    assert abs(sum(n['mass_fraction_candidate'] for n in nodes)-1)<1e-6
    sides.append(dict(side=str(side.get_editor_property('side')),anchor_index=side.get_editor_property('anchor_bone'),
        volume_cm3=side.get_editor_property('effective_volume_cm3'),com=v(side.get_editor_property('com')),dimensions=v(side.get_editor_property('dimensions')),
        support_area_cm2=side.get_editor_property('support_area_cm2'),region_vertices=sum(w>.001 for w in weights),max_weight=max(weights),
        shape_responses=len(side.get_editor_property('shape_responses')),fold_references=[v(p) for p in side.get_editor_property('fold_references')],regions=nodes))
shape_values={}
shape_test=os.environ.get('VAM_GLUTE_TEST_REPORT')
if shape_test:
    import re
    tests=json.loads(Path(shape_test).read_text(encoding='utf-8-sig'))
    for test in tests['tests']:
        assert test['state']=='Success'
        for entry in test.get('entries',[]):
            match=re.search(r'G0_SHAPE_PARAMETER (\S+)=([-0-9.]+)',entry['event']['message'])
            if match:shape_values[match.group(1)]=float(match.group(2))
parameters={str(p.get_editor_property('target')):p for p in definition.get_editor_property('parameters')}
for name,value in shape_values.items():
    assert name in parameters
    assert parameters[name].get_editor_property('minimum')<=value<=parameters[name].get_editor_property('maximum')
map_path='/Game/GluteStructureEngineering/Empty_'+report['identity'][:12]
if not u.EditorAssetLibrary.does_asset_exist(map_path):
    world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
    bp=u.load_asset(report['blueprint'])
    actor=u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(bp.generated_class(),u.Vector())
    assert actor
    if shape_values:
        variant=u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(bp.generated_class(),u.Vector(0,250,0))
        variant.get_editor_property('character').set_editor_property('initial_shape_values',shape_values)
        variant.set_actor_label('G0 supported Shape variation')
    assert u.EditorLoadingAndSavingUtils.save_map(world,map_path)
result=dict(configuration=report['configuration'],blueprint=report['blueprint'],profile=profile.get_path_name(),body=definition.get_editor_property('body').get_path_name(),
    map=map_path,source_bones=len(original),final_bones=len(derived),appended_helpers=derived[len(original):],sides=sides,
    source_topology=str(profile.get_editor_property('source_topology_identity')),family=str(profile.get_editor_property('skeleton_family')),
    shape_variant={name:dict(value=value,display_name=parameters[name].get_editor_property('display_name'),source_id=parameters[name].get_editor_property('source_id')) for name,value in shape_values.items()},
    algorithm=str(profile.get_editor_property('algorithm')),provenance=str(profile.get_editor_property('region_provenance')),
    morph_parameters=len(definition.get_editor_property('parameters')),parts=len(definition.get_editor_property('parts')),
    checks=dict(source_indices_and_binds_preserved=True,helper_hierarchy=True,weights_normalized=True,influence_limit=8,zero_bind_tolerance_cm=.001,morphs_native_build_validated=True,independent_reload=True),visual_assessment=None)
report_path.with_name('audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
u.log('VAM_GLUTE_AUDIT '+json.dumps(result,ensure_ascii=False))
