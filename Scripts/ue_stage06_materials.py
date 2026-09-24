"""Clone source-reference graphs into Stage06-owned shader categories, preserving UV/alpha/normal wiring."""
import hashlib,json,traceback
from pathlib import Path
import unreal as u

ROOT='/Game/VamStage06'
SRC='/Game/VamCharacters/C_c6e9958caee9598900461d95'
REPORT=Path(u.Paths.project_dir(),'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-materials.json')

def category(evidence,semantic):
    text=evidence.lower()
    if 'hidden' in text:return 'reference'
    for tag in ('cornea','eyereflection','tear','lacrimals'):
        if tag in text:return 'eye_clear'
    for tag in ('irises','pupils','sclera'):
        if tag in text:return 'eye'
    for tag in ('teeth','gums','tongue','innermouth'):
        if tag in text:return 'mouth'
    if 'fingernail' in text or 'toenail' in text:return 'nail'
    if semantic in ('face','torso','limbs','genitals'):return 'skin'
    return 'reference'

def parameters(mat,kind):
    values={'skin':(.42,.32),'eye_clear':(.04,.8),'eye':(.2,.5),'mouth':(.24,.46),
            'nail':(.28,.5),'fabric':(.68,.28),'reference':(.55,.5)}
    rough,spec=values[kind]
    edit=u.MaterialEditingLibrary
    for name,value,prop in [('Roughness',rough,u.MaterialProperty.MP_ROUGHNESS),('Specular',spec,u.MaterialProperty.MP_SPECULAR)]:
        expression=edit.create_material_expression(mat,u.MaterialExpressionScalarParameter)
        expression.set_editor_property('parameter_name',name)
        expression.set_editor_property('default_value',value)
        assert edit.connect_material_property(expression,'',prop)
    if kind=='skin' and mat.get_editor_property('blend_mode')==u.BlendMode.BLEND_OPAQUE:
        try:
            mat.set_editor_property('shading_model',u.MaterialShadingModel.MSM_SUBSURFACE)
            color=edit.create_material_expression(mat,u.MaterialExpressionConstant3Vector)
            color.set_editor_property('constant',u.LinearColor(.65,.23,.17,1))
            edit.connect_material_property(color,'',u.MaterialProperty.MP_SUBSURFACE_COLOR)
        except Exception as exc:
            u.log_warning('Stage06 skin subsurface unavailable: '+str(exc))
    edit.recompile_material(mat)
    edit.set_material_usage(mat,u.MaterialUsage.MATUSAGE_SKELETAL_MESH)
    edit.set_material_usage(mat,u.MaterialUsage.MATUSAGE_MORPH_TARGETS)

def tint_parameter(mat):
    if u.EditorAssetLibrary.get_metadata_tag(mat,'VamStage06TintReady')=='1':return
    edit=u.MaterialEditingLibrary
    prior=edit.get_material_property_input_node(mat,u.MaterialProperty.MP_BASE_COLOR)
    assert prior,'Source graph has no base color node'
    output=edit.get_material_property_input_node_output_name(mat,u.MaterialProperty.MP_BASE_COLOR)
    tint=edit.create_material_expression(mat,u.MaterialExpressionVectorParameter)
    tint.set_editor_property('parameter_name','Tint')
    tint.set_editor_property('default_value',u.LinearColor.WHITE)
    multiply=edit.create_material_expression(mat,u.MaterialExpressionMultiply)
    assert edit.connect_material_expressions(prior,output,multiply,'A')
    assert edit.connect_material_expressions(tint,'',multiply,'B')
    assert edit.connect_material_property(multiply,'',u.MaterialProperty.MP_BASE_COLOR)
    edit.recompile_material(mat)
    u.EditorAssetLibrary.set_metadata_tag(mat,'VamStage06TintReady','1')

try:
    definition=u.load_asset(SRC+'/CD_Character')
    body=definition.get_editor_property('body')
    parts=definition.get_editor_property('parts')
    regions=u.load_asset(SRC+'/GD_Bindings').get_editor_property('regions')
    materials=[];part_materials=[];cache={};semantic_counts={}
    def clone(source,kind):
        if not source or kind=='reference':return source
        key=(source.get_path_name(),kind)
        if key in cache:return cache[key]
        name='M_'+kind+'_'+hashlib.sha256((key[0]+'|'+kind).encode()).hexdigest()[:12]
        path=ROOT+'/Materials/'+name
        result=u.load_asset(path)
        if not result:
            result=u.EditorAssetLibrary.duplicate_asset(source.get_path_name(),path)
            assert result,path
            parameters(result,kind)
        tint_parameter(result)
        assert u.EditorAssetLibrary.save_loaded_asset(result)
        cache[key]=result
        semantic_counts[kind]=semantic_counts.get(kind,0)+1
        return result
    for index,slot in enumerate(body.get_editor_property('materials')):
        source=slot.get_editor_property('material_interface')
        region=regions[index]
        kind=category(str(region.get_editor_property('source_evidence')),str(region.get_editor_property('anatomical_semantic')))
        materials.append(clone(source,kind))
    for mesh in parts:
        row=u.VamPartMaterialSet()
        row.set_editor_property('materials',[clone(slot.get_editor_property('material_interface'),'fabric') for slot in mesh.get_editor_property('materials')])
        part_materials.append(row)
    profile=u.load_asset(ROOT+'/DA_NativeMaterials')
    if not profile:
        factory=u.DataAssetFactory();factory.set_editor_property('data_asset_class',u.VamMaterialProfile)
        profile=u.AssetToolsHelpers.get_asset_tools().create_asset('DA_NativeMaterials',ROOT,u.VamMaterialProfile,factory)
    profile.set_editor_property('body_materials',materials)
    profile.set_editor_property('part_materials',part_materials)
    assert u.EditorAssetLibrary.save_loaded_asset(profile)
    bp=u.load_asset(ROOT+'/BP_VamCharacter')
    c=u.get_default_object(bp.generated_class()).get_editor_property('character')
    c.set_editor_property('material_profile',profile)
    u.BlueprintEditorLibrary.compile_blueprint(bp)
    assert u.EditorAssetLibrary.save_loaded_asset(bp)
    REPORT.write_text(json.dumps({'status':'complete','profile':profile.get_path_name(),'body_slots':len(materials),
                                  'part_count':len(part_materials),'unique_materials':len(cache),'semantic_counts':semantic_counts},indent=2),encoding='utf8')
except Exception:
    REPORT.write_text(json.dumps({'status':'failed','error':traceback.format_exc()},indent=2),encoding='utf8')
    raise
