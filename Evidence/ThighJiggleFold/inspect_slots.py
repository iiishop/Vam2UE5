import unreal as u,json
from pathlib import Path
r=Path('I:/Document/UE5/SmartNPC/Plugins/VamResourceBrowser/Saved/LowerBodyContact/ThighJiggleFold')
b=json.loads((r/'bundle.json').read_text());root=b['configuration'].rsplit('/',1)[0]
source=u.load_asset(root+'/Glute/CD_Character');assert source
out='/Game/VamThighJiggleSlotAudit'
assert not u.EditorAssetLibrary.does_asset_exist(out+'/DA_GluteStructure')
glute=u.EditorAssetLibrary.duplicate_asset(root+'/DA_GluteStructure',out+'/DA_GluteStructure');assert glute
factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',u.VamLegJiggleProfile)
leg=u.AssetToolsHelpers.get_asset_tools().create_asset('DA_LegJiggle',out,u.VamLegJiggleProfile,factory);assert leg
family=json.loads((r.parent/'ThighSeamResidualOnly/candidate-3/family.json').read_text())
definition,error=u.VamLegJiggleBuilder.build(out+'/Leg',source,leg,glute,json.dumps(family))
shape=source.get_editor_property('shape');geometry=shape.get_editor_property('geometry')
ids=list(geometry.get_editor_property('input_to_source'))
rows=[]
for segment in leg.get_editor_property('segments'):
 weights=list(segment.get_editor_property('region_weights'))
 rows.append(dict(name=str(segment.get_editor_property('name')),vertices=[dict(input=i,source=s,region_weight=weights[i]) for i,s in enumerate(ids) if s in [11274,16801]]))
(r/'slots/result.json').write_text(json.dumps(dict(source=source.get_path_name(),configuration=b['configuration'],identity=b['identity'],build_returned=bool(definition),error=str(error),regions=rows,scope='Isolated Leg builder replay from frozen Glute mesh; duplicate final Glute profile only, no production assets modified. Input and transfer weights captured before downstream Glute profile edits.'),indent=2))
assert definition,str(error)
