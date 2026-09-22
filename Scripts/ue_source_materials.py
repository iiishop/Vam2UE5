"""Unsaved UE reference materials. This is not a Canonical/Game material adapter."""
import json
from pathlib import Path
import unreal


class Appearance:
    def __init__(self,path):
        self.ir=json.loads(Path(path).read_text(encoding='utf8'))
        self.folder='/Game/SourceAppearanceReference/R2_M'+self.ir['material_id'][:16]
        self.textures={};self.materials={};self.errors=[]
        self.bindings={(m['binding']['mesh'],m['binding']['slot']):m for m in self.ir['materials']}

    def texture(self,record):
        asset=record.get('asset')
        if not asset:return None
        normal=record['semantic']=='normal'
        if normal and asset['encoding']=='unity_decoded_pixels':return None
        key=(asset['sha256'],record['color_space'],normal)
        if key in self.textures:return self.textures[key]
        task=unreal.AssetImportTask();task.filename=asset['file'];task.destination_path=self.folder
        task.destination_name='T_'+asset['sha256'][:20]+('_N' if normal else '_S' if record['color_space']=='sRGB' else '_L')
        task.automated=True;task.save=False;task.replace_existing=True
        unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])
        if not task.imported_object_paths:raise RuntimeError('Texture import returned no object: '+asset['file'])
        texture=unreal.load_asset(task.imported_object_paths[0]);texture.set_editor_property('srgb',record['color_space']=='sRGB')
        texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP if normal else unreal.TextureCompressionSettings.TC_DEFAULT)
        if normal:texture.set_editor_property('flip_green_channel',True)
        self.textures[key]=texture;return texture

    def material(self,mesh,slot):
        record=self.bindings.get((mesh,slot))
        if not record:return None,False
        if record['render_state']['hidden']:return None,True
        if record['id'] in self.materials:return self.materials[record['id']],False
        try:
            mat=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_'+record['id'][:20],self.folder,unreal.Material,unreal.MaterialFactoryNew())
            edit=unreal.MaterialEditingLibrary
            def node(cls):return edit.create_material_expression(mat,cls)
            def scalar(value):
                n=node(unreal.MaterialExpressionConstant);n.set_editor_property('r',float(value));return n
            def vector(value):
                n=node(unreal.MaterialExpressionConstant3Vector);n.set_editor_property('constant',unreal.LinearColor(*value[:3],1));return n
            def connect(source,name,target,pin):edit.connect_material_expressions(source,name,target,pin)
            def multiply(a,b):
                n=node(unreal.MaterialExpressionMultiply);connect(a,'',n,'A');connect(b,'',n,'B');return n
            def sample(prop):
                tex_record=record['textures'].get(prop)
                if not tex_record:return None
                tex=self.texture(tex_record)
                if not tex:return None
                n=node(unreal.MaterialExpressionTextureSample);n.set_editor_property('texture',tex)
                sampler=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL if tex_record['semantic']=='normal' else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if tex_record['color_space']=='sRGB' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR
                n.set_editor_property('sampler_type',sampler)
                uv=node(unreal.MaterialExpressionTextureCoordinate)
                sx,sy=tex_record['ue_scale'];uv.set_editor_property('u_tiling',sx);uv.set_editor_property('v_tiling',sy)
                ox,oy=tex_record['ue_offset']
                offset=node(unreal.MaterialExpressionConstant2Vector);offset.set_editor_property('r',ox);offset.set_editor_property('g',oy)
                add=node(unreal.MaterialExpressionAdd);connect(uv,'',add,'A');connect(offset,'',add,'B');connect(add,'',n,'Coordinates')
                return n
            params=record['parameters'];tint=params.get('_Color',[1,1,1,1]);base=vector(tint)
            diffuse=sample('_MainTex')
            if diffuse:base=multiply(diffuse,base)
            decal=sample('_DecalTex')
            if decal:
                blend=node(unreal.MaterialExpressionLinearInterpolate);connect(base,'',blend,'A');connect(decal,'RGB',blend,'B');connect(decal,'A',blend,'Alpha');base=blend
            edit.connect_material_property(base,'',unreal.MaterialProperty.MP_BASE_COLOR)
            edit.connect_material_property(scalar(.55),'',unreal.MaterialProperty.MP_ROUGHNESS)
            normal=sample('_BumpMap')
            if normal:edit.connect_material_property(normal,'RGB',unreal.MaterialProperty.MP_NORMAL)
            blend_mode=record['render_state']['blend']
            # Older cached IR labelled premultiplied transparency as unknown.
            state=record['render_state'].get('source_first_pass',{}).get('rtBlend0',{})
            if (state.get('srcBlend',{}).get('val'),state.get('destBlend',{}).get('val'))==(1.,10.):
                blend_mode='translucent'
            if blend_mode in ('masked','translucent'):
                mat.set_editor_property('blend_mode',unreal.BlendMode.BLEND_MASKED if blend_mode=='masked' else unreal.BlendMode.BLEND_TRANSLUCENT)
                alpha=sample('_AlphaTex');opacity=scalar(1.)
                if 'SeparateAlpha' not in (record['source_shader'].get('name') or ''):
                    opacity=scalar(tint[3] if len(tint)>3 else 1.)
                    if diffuse:
                        combined=node(unreal.MaterialExpressionMultiply);connect(diffuse,'A',combined,'A');connect(opacity,'',combined,'B');opacity=combined
                if alpha:
                    asset=record['textures']['_AlphaTex']['asset']
                    if asset['encoding']=='source_image_pixels':
                        dot=node(unreal.MaterialExpressionDotProduct);connect(alpha,'RGB',dot,'A');connect(vector([1/3]*3),'',dot,'B');opacity=dot
                    else:
                        opacity=node(unreal.MaterialExpressionMultiply);connect(alpha,'A',opacity,'A');connect(scalar(1.),'',opacity,'B')
                adjust=node(unreal.MaterialExpressionAdd);connect(opacity,'',adjust,'A');connect(scalar(params.get('_AlphaAdjust',0)),'',adjust,'B')
                clamp=node(unreal.MaterialExpressionClamp);connect(adjust,'',clamp,'Input')
                edit.connect_material_property(clamp,'',unreal.MaterialProperty.MP_OPACITY_MASK if blend_mode=='masked' else unreal.MaterialProperty.MP_OPACITY)
                mat.set_editor_property('opacity_mask_clip_value',float(params.get('_Cutoff',.3)))
            mat.set_editor_property('two_sided',bool(record['render_state']['two_sided']))
            edit.recompile_material(mat);self.materials[record['id']]=mat
            return mat,False
        except Exception as exc:
            self.errors.append({'binding':record['binding'],'error':str(exc)});return None,False

    def scene(self,subsystem,interactive):
        settings=self.ir['validation_scene']
        camera=subsystem.spawn_actor_from_class(unreal.CameraActor,unreal.Vector(*settings['camera_location_cm']),unreal.Rotator(*settings['camera_rotation_deg']),transient=True)
        camera.set_actor_label('Source Appearance Reference - fixed camera')
        camera.camera_component.set_editor_property('field_of_view',settings['fov'])
        volume=subsystem.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector(),transient=True)
        volume.set_editor_property('unbound',True)
        pp=volume.get_editor_property('settings')
        pp.set_editor_property('override_auto_exposure_method',True);pp.set_editor_property('auto_exposure_method',unreal.AutoExposureMethod.AEM_MANUAL)
        pp.set_editor_property('override_auto_exposure_apply_physical_camera_exposure',True);pp.set_editor_property('auto_exposure_apply_physical_camera_exposure',False)
        pp.set_editor_property('override_auto_exposure_bias',True);pp.set_editor_property('auto_exposure_bias',0.)
        volume.set_editor_property('settings',pp)
        for label,rotation,intensity in [('key',(-35,-30,0),settings['key_intensity']),('fill',(-20,140,0),settings['fill_intensity'])]:
            light=subsystem.spawn_actor_from_class(unreal.DirectionalLight,unreal.Vector(0,0,250),unreal.Rotator(*rotation),transient=True)
            light.set_actor_label('Source Appearance Reference - '+label);light.light_component.set_intensity(intensity)
        if interactive:unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).pilot_level_actor(camera)
