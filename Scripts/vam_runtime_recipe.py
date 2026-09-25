"""Pure validation/identity contract for runtime upgrades. No machine-local source discovery."""
import hashlib
import json
import math
import re

ALGORITHM = 'runtime-bundle-v1-parent-frame'

def digest(value):
    return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':'),ensure_ascii=False,allow_nan=False).encode()).hexdigest()

def require(condition, message):
    if not condition: raise ValueError(message)

def validate_recipe(recipe):
    require(recipe.get('schema')=='vam-runtime-recipe/1','Unsupported runtime recipe schema')
    for key in ('definition','source_mapping','destination_root'):
        require(isinstance(recipe.get(key),str) and re.fullmatch(r'/Game(?:/[A-Za-z_][A-Za-z0-9_]*)+(?:\.[A-Za-z_][A-Za-z0-9_]*)?',recipe[key]),'Explicit /Game asset path required: '+key)
    require('.' not in recipe['destination_root'],'destination_root must be a package directory')
    require(recipe.get('family'),'Explicit skeleton family policy file required')
    require(recipe.get('skin_shading','source') in ('source','subsurface'),'Unsupported skin_shading policy')
    require(2 <= recipe.get('minimum_bone_size_cm',8) <= 30,'minimum_bone_size_cm must be in [2,30]')
    tissue=recipe.get('soft_tissue')
    if tissue is not None:
        require(isinstance(tissue,dict) and tissue.get('regions'),'Explicit soft tissue regions required')
        require(tissue.get('quality','Balanced') in ('Off','Balanced','High'),'Unknown soft tissue quality')
        names=[r.get('name') for r in tissue['regions']]
        require(all(isinstance(n,str) and n for n in names) and len(set(names))==len(names),'Duplicate or missing tissue region name')
        require(set(tissue.get('enabled_regions',[])) <= set(names),'Unknown enabled tissue region')
        require(isinstance(tissue.get('gravity',True),bool),'gravity must be boolean')
        for region in tissue['regions']:
            require(region.get('bone'),'Explicit support bone required')
            require(isinstance(region.get('cells',4),int) and 2<=region.get('cells',4)<=8,'cells must be in [2,8]')
            require(0<=region.get('minimum_skin_weight',.05)<region.get('full_skin_weight',.5)<=1,'Invalid skin support weights')
            require(0<region.get('support_fraction',.25)<1,'Invalid support fraction')
            for field,default in [('density_kg_per_cm3',.001),('stiffness',100000)]:
                require(math.isfinite(region.get(field,default)) and region.get(field,default)>0,'Invalid '+field)
            require(0<=region.get('damping',.1)<=1 and 0<=region.get('incompressibility',.45)<.5,'Invalid material coefficients')

def check_bones(bones):
    require(bones and len({b['name'] for b in bones})==len(bones),'Empty or duplicate bone names')
    for i,b in enumerate(bones):
        require(isinstance(b['parent'],int) and -1 <= b['parent'] < i,'Parent must precede child: '+b['name'])
        for key,n in (('translation',3),('quaternion_xyzw',4)):
            require(len(b[key])==n and all(math.isfinite(x) for x in b[key]),'Invalid '+key+': '+b['name'])
        require(abs(sum(x*x for x in b['quaternion_xyzw'])-1)<1.e-4,'Non-unit local rotation: '+b['name'])
        require(all(abs(x-1)<1.e-6 for x in b.get('scale',[1,1,1])),'Non-unit bone scale is not supported')

def verify_native_binding(actual, source):
    check_bones(actual);check_bones(source)
    require(len(actual)==len(source),'Mesh/source bone count mismatch')
    for a,b in zip(actual,source):
        require(a['name']==b['name'] and a['parent']==b['parent'],'Mesh/source hierarchy mismatch')
        require(math.dist(a['translation'],b['translation'])<1.e-4,'Mesh/source bind translation mismatch: '+a['name'])
        dot=abs(sum(x*y for x,y in zip(a['quaternion_xyzw'],b['quaternion_xyzw'])))
        require(dot>1-1.e-7,'Mesh/source local axes mismatch: '+a['name'])

def validate_family(neutral_bones, family):
    check_bones(neutral_bones)
    require(family.get('schema')=='vam-rig-family/1' and family.get('family'),'Unsupported family policy')
    expected=family['bones']; actual={b['name']:b for b in neutral_bones}
    require(set(actual)==set(expected),'Skeleton family bone set mismatch; author another family policy')
    tolerance=family['maximum_axis_error_degrees']
    require(0 <= tolerance <= 5,'Family axis tolerance must be at most five degrees')
    for b in neutral_bones:
        template=expected[b['name']]
        parent=neutral_bones[b['parent']]['name'] if b['parent']>=0 else None
        require(parent==template['parent'],'Family parent mismatch: '+b['name'])
        q=template['quaternion_xyzw']
        require(len(q)==4 and all(math.isfinite(x) for x in q) and abs(sum(x*x for x in q)-1)<1.e-4,'Invalid family axes')
        dot=min(1,abs(sum(x*y for x,y in zip(q,b['quaternion_xyzw']))))
        require(math.degrees(2*math.acos(dot))<=tolerance+1.e-5,'Family axes mismatch: '+b['name'])
    joints=family['joints']
    require(len({j['semantic'] for j in joints})==len(joints),'Duplicate joint semantics')
    require(len({j['bone'] for j in joints})==len(joints),'Duplicate mapped bones')
    for joint in joints:
        require(joint['bone'] in actual,'Missing mapped bone: '+joint['bone'])
        for key in ('minimum','maximum','preferred_bend'):
            require(len(joint[key])==3 and all(math.isfinite(x) for x in joint[key]),'Invalid joint policy: '+key)
        require(all(-175 <= lo <= 0 <= hi <= 175 for lo,hi in zip(joint['minimum'],joint['maximum'])),'Joint bounds must contain the reference pose')
    semantics={j['semantic'] for j in joints}
    require(family['solver_root'] in semantics and set(family['effectors'])<=semantics,'Unknown solver root/effector')
    return joints

def build_identity(recipe, family, input_fingerprints, algorithm_fingerprints, engine_version):
    validate_recipe(recipe)
    return digest({'schema':ALGORITHM,'recipe':recipe,'family':family,'inputs':input_fingerprints,
                   'algorithms':algorithm_fingerprints,'engine':engine_version})
