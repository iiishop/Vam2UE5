import copy,unittest
from vam_runtime_recipe import validate_family,verify_native_binding,build_identity,validate_recipe

class RuntimeRecipeTests(unittest.TestCase):
    def setUp(self):
        self.bones=[{'name':'root','parent':-1,'translation':[0,0,0],'quaternion_xyzw':[0,0,0,1]},
                    {'name':'joint','parent':0,'translation':[0,0,10],'quaternion_xyzw':[0,0,0,1]}]
        self.family={'schema':'vam-rig-family/1','family':'test','maximum_axis_error_degrees':.1,
          'bones':{'root':{'parent':None,'quaternion_xyzw':[0,0,0,1]},'joint':{'parent':'root','quaternion_xyzw':[0,0,0,1]}},
          'joints':[{'bone':'root','semantic':'root','pose_control':False,'minimum':[0,0,0],'maximum':[0,0,0],'preferred_bend':[0,0,0]}],
          'solver_root':'root','effectors':[]}
        self.recipe={'schema':'vam-runtime-recipe/1','definition':'/Game/A/CD','source_mapping':'/Game/A/Mapping','destination_root':'/Game/Runtime','family':'family.json'}
    def test_proportions_do_not_copy_template_lengths(self):
        other=copy.deepcopy(self.bones);other[1]['translation']=[0,0,24]
        validate_family(other,self.family)
        with self.assertRaisesRegex(ValueError,'translation mismatch'):verify_native_binding(other,self.bones)
    def test_rejects_changed_local_axes(self):
        other=copy.deepcopy(self.bones);other[1]['quaternion_xyzw']=[0,0,1,0]
        with self.assertRaisesRegex(ValueError,'axes mismatch'):validate_family(other,self.family)
    def test_quaternion_sign_is_not_a_different_axis_frame(self):
        other=copy.deepcopy(self.bones);other[1]['quaternion_xyzw']=[0,0,0,-1]
        validate_family(other,self.family);verify_native_binding(other,self.bones)
    def test_rejects_changed_parent(self):
        other=copy.deepcopy(self.bones);other[1]['parent']=-1
        with self.assertRaisesRegex(ValueError,'parent mismatch'):validate_family(other,self.family)
    def test_rejects_cycles_and_scale(self):
        for key,value in [('parent',1),('scale',[1,2,1])]:
            other=copy.deepcopy(self.bones);other[1][key]=value
            with self.assertRaises(ValueError):validate_family(other,self.family)
    def test_explicit_source_required(self):
        del self.recipe['source_mapping']
        with self.assertRaises(ValueError):validate_recipe(self.recipe)
    def test_shading_policy_changes_identity_and_rejects_unknown(self):
        a=build_identity(self.recipe,self.family,{}, {},'engine')
        self.recipe['skin_shading']='subsurface'
        self.assertNotEqual(a,build_identity(self.recipe,self.family,{}, {},'engine'))
        self.recipe['skin_shading']='typo'
        with self.assertRaisesRegex(ValueError,'skin_shading'):validate_recipe(self.recipe)
    def test_duplicate_semantics_rejected(self):
        self.family['joints']*=2
        with self.assertRaisesRegex(ValueError,'Duplicate'):validate_family(self.bones,self.family)
    def test_tissue_settings_are_identity_inputs(self):
        original=build_identity(self.recipe,self.family,{}, {},'engine')
        self.recipe['soft_tissue']={'regions':[{'name':'region','bone':'joint'}],'enabled_regions':['region'],'quality':'Balanced'}
        validate_recipe(self.recipe)
        first=build_identity(self.recipe,self.family,{}, {},'engine')
        self.assertNotEqual(original,first)
        self.recipe['soft_tissue']['quality']='High'
        self.assertNotEqual(first,build_identity(self.recipe,self.family,{}, {},'engine'))
    def test_tissue_unknown_region_and_invalid_material_rejected(self):
        self.recipe['soft_tissue']={'regions':[{'name':'region','bone':'joint'}],'enabled_regions':['missing']}
        with self.assertRaisesRegex(ValueError,'Unknown enabled'):validate_recipe(self.recipe)
        self.recipe['soft_tissue']['enabled_regions']=['region']
        self.recipe['soft_tissue']['regions'][0]['density_kg_per_cm3']=float('nan')
        with self.assertRaisesRegex(ValueError,'density'):validate_recipe(self.recipe)
    def test_all_derivation_inputs_change_identity(self):
        base=build_identity(self.recipe,self.family,{'mesh':'a','morphset':'b','material':'c'},{'algorithm':'x'},'engine')
        for key in ('mesh','morphset','material'):
            changed={'mesh':'a','morphset':'b','material':'c'};changed[key]='changed'
            self.assertNotEqual(base,build_identity(self.recipe,self.family,changed,{'algorithm':'x'},'engine'))
        self.assertNotEqual(base,build_identity(self.recipe,self.family,{'mesh':'a','morphset':'b','material':'c'},{'algorithm':'y'},'engine'))
        changed=copy.deepcopy(self.family);changed['maximum_axis_error_degrees']=.05
        self.assertNotEqual(base,build_identity(self.recipe,changed,{'mesh':'a','morphset':'b','material':'c'},{'algorithm':'x'},'engine'))
    def test_nonfinite_policy_rejected(self):
        self.family['joints'][0]['preferred_bend'][0]=float('nan')
        with self.assertRaises(ValueError):validate_family(self.bones,self.family)

if __name__=='__main__':unittest.main()
