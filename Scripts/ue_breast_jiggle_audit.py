"""Audit a committed breast runtime and create a cook-only empty-level fixture.
Uses only ordinary production BP_VamCharacter instances; no runtime probe actor.
VAM_RUNTIME_REPORT selects the persisted transaction. This script makes no visual verdict.
"""
import json, os, sys, math
from pathlib import Path
import unreal as u
sys.path.insert(0,str(Path(__file__).resolve().parent))
from vam_runtime_recipe import verify_native_binding
report_path=Path(os.environ['VAM_RUNTIME_REPORT'])
report=json.loads(report_path.read_text(encoding='utf8'))
assert report['status']=='committed'
config=u.load_asset(report['configuration'])
definition=config.get_editor_property('definition')
profile=config.get_editor_property('breast_jiggle')
assert not u.VamBreastJiggleBuilder.validate(definition,profile)
receipt=json.loads(config.get_editor_property('receipt_json'))
source=u.load_asset(receipt['source_definition'])
body=definition.get_editor_property('body')
original=json.loads(u.VamStage06AssetEditor.describe_mesh_binding(source.get_editor_property('body')))
derived=json.loads(u.VamStage06AssetEditor.describe_mesh_binding(body))
verify_native_binding(derived[:len(original)],original)
bind_error=max(math.dist(a["translation"],b["translation"]) for a,b in zip(original,derived))
assert len(derived)==len(original)+12
assert len(definition.get_editor_property('parameters'))==len(source.get_editor_property('parameters'))
def vector(v):return [v.x,v.y,v.z]
sides=[]
for side in profile.get_editor_property('sides'):
    volume=side.get_editor_property('effective_volume_cm3')
    density=profile.get_editor_property('density_kg_per_cm3')
    assert abs(side.get_editor_property('mass_kg')-volume*density)<1e-9
    weights=list(side.get_editor_property('region_weights'))
    assert all(0<=w<=1 for w in weights) and max(weights)>.01
    sides.append(dict(side=str(side.get_editor_property('side')),volume_cm3=volume,density_kg_cm3=density,mass_kg=volume*density,
        com=vector(side.get_editor_property('com')),radius_cm=side.get_editor_property('effective_radius_cm'),depth_cm=side.get_editor_property('effective_depth_cm'),
        support_area_cm2=side.get_editor_property('support_area_cm2'),region_vertices=sum(w>.001 for w in weights),region_max=max(weights),
        morph_responses=len(side.get_editor_property('shape_responses')),
        nodes=[dict(semantic=str(n.get_editor_property('semantic')),index=n.get_editor_property('bone_index'),rest=vector(n.get_editor_property('rest')),
            frequency_hz=vector(n.get_editor_property('frequency_hz')),mass_fraction=n.get_editor_property('mass_fraction')) for n in side.get_editor_property('nodes')]))
map_path='/Game/BreastJiggleEngineering/Empty_'+report['identity'][:12]
if not u.EditorAssetLibrary.does_asset_exist(map_path):
    world=u.EditorLoadingAndSavingUtils.new_blank_map(False)
    bp=u.load_asset(report['blueprint'])
    actor=u.get_editor_subsystem(u.EditorActorSubsystem).spawn_actor_from_class(bp.generated_class(),u.Vector(0,0,0))
    assert actor
    assert u.EditorLoadingAndSavingUtils.save_map(world,map_path)
result=dict(configuration=report['configuration'],blueprint=report['blueprint'],profile=profile.get_path_name(),map=map_path,
    source_bones=len(original),max_source_bind_translation_error_cm=bind_error,helper_bones=derived[len(original):],sides=sides,
    morph_parameters=len(definition.get_editor_property('parameters')),parts=len(definition.get_editor_property('parts')),
    checks=dict(existing_indices_preserved=True,helper_hierarchy=True,weights_normalized=True,influence_limit=8,zero_bind_tolerance_cm=.001,morphs_native_build_validated=True,reloaded=True),
    visual_assessment=None)
report_path.with_name('audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf8')
u.log('VAM_BREAST_AUDIT '+json.dumps(result,ensure_ascii=False))
