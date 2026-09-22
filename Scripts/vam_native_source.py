"""Stage05 source contract. Never use the already morphed preview as X0.

This module has no UE/UnityPy dependency. It consumes immutable Stage03 IR only.
TriAx and bone/formula evaluation remain explicit build blockers until calibrated.
"""
import copy
import hashlib
import json
import math
from pathlib import Path

from vam_decode import require, finite, preview_mesh, to_ue
from vam_fit import apply_morph, apply_graft_boundary
from vam_ue_mesh import normals_for_ue


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=False, allow_nan=False).encode()).hexdigest()


def qmul(a, b):
    x,y,z,w=a; X,Y,Z,W=b
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z]


def qinverse(q):
    return [-q[0],-q[1],-q[2],q[3]]


def rotate(q, p):
    return qmul(qmul(q, [*p,0]), qinverse(q))[:3]


def matrix(q, t):
    columns=[rotate(q, axis) for axis in ([1,0,0],[0,1,0],[0,0,1])]
    return [[columns[j][i] for j in range(3)]+[t[i]] for i in range(3)]+[[0,0,0,1]]


def reference_bones(source):
    """DAZBone.SetTransformToImportValues: orientation is NOT the pose rotation order.

    VaM uses qZ*qY*qX, or Unity Euler qY*qX*qZ when explicitly selected.
    Basis C:(x,y,z)->(z,x,y) has det +1, so C*q*C^-1 permutes the quaternion
    vector part, NOT Euler angles. World reference poses are then made local.
    """
    require(source, 'skeleton_missing', 'No source bones')
    lookup={b['name']:b for b in source}
    require(len(lookup)==len(source), 'bone_name', 'Duplicate source bone names')
    roots=[b for b in source if not b['parent']]
    output=[]; indexes={}; visiting=set(); worlds={}
    if len(roots)>1:
        require('__VamSourceRoot' not in lookup,'bone_name','Reserved assembly root collision')
        identity=matrix([0,0,0,1],[0,0,0])
        output.append({'name':'__VamSourceRoot','source_id':None,'parent':-1,
                       'translation':[0,0,0],'quaternion_xyzw':[0,0,0,1],
                       'world_bind':identity,'inverse_bind':identity,
                       'semantic':'synthetic_identity_forest_root'})
    def visit(name):
        if name in indexes:return
        require(name not in visiting, 'bone_cycle', name)
        require(name in lookup, 'bone_parent', name)
        visiting.add(name); b=lookup[name]; parent=b['parent']
        if parent:visit(parent)
        qs=[]
        for axis,angle in enumerate(b['orientation_degrees']):
            q=[0.,0.,0.,math.cos(math.radians(angle)/2)]
            q[axis]=math.sin(math.radians(angle)/2);qs.append(q)
        unity=b['parameters'].get('useUnityEulerOrientation')
        require(unity in (0,1,False,True), 'orientation_mode', name)
        order=(1,0,2) if unity else (2,1,0)
        q=[0.,0.,0.,1.]
        for axis in order:q=qmul(q,qs[axis])
        q=[q[2],q[0],q[1],q[3]]
        position=to_ue(b.get('base_position', b['position']))
        worlds[name]=(q,position)
        pq,pt=worlds[parent] if parent else ([0,0,0,1],[0,0,0])
        local_q=qmul(qinverse(pq),q)
        local_t=rotate(qinverse(pq),[a-c for a,c in zip(position,pt)])
        reconstructed=[a+c for a,c in zip(rotate(pq,local_t),pt)]
        require(max(abs(a-c) for a,c in zip(position,reconstructed))<1e-7, 'bind_roundtrip', name)
        iq=qinverse(q); inverse_t=rotate(iq,[-v for v in position])
        indexes[name]=len(output)
        output.append({'name':name,'source_id':b['source_object'],
                       'parent':indexes[parent] if parent else (0 if len(roots)>1 else -1),
                       'translation':local_t,'quaternion_xyzw':local_q,
                       'world_bind':matrix(q,position),'inverse_bind':matrix(iq,inverse_t),
                       'pose_rotation_order':b['rotation_order'],
                       'reference_orientation_mode':'UnityZXY' if unity else 'DAZ_ZYX'})
        visiting.remove(name)
    for name in sorted(lookup):visit(name)
    require(sum(b['parent']==-1 for b in output)==1,'skeleton_roots','Exactly one source root required')
    return output


def general_weights(raw, count, bones):
    """The verified DAZSkinV2 general branch ignores fullyWeightedVertices.

    Those vertices belong to the TriAx branch, not an extra general influence.
    Reject uncovered or unnormalized data instead of manufacturing root weights.
    """
    require(bool(raw['_useGeneralWeights']) and bool(raw['_hasGeneralWeights']),
            'triax_pending_calibration','No verified LBS fit / held-out pose reference')
    bone_map={b['name']:i for i,b in enumerate(bones)}
    weights=[{} for _ in range(count)]
    for node in raw['nodes']:
        require(node['name'] in bone_map,'skin_bone',node['name'])
        bone=bone_map[node['name']]
        for row in node['generalWeights']:
            v=row['vertex'];w=row['weight']
            require(isinstance(v,int) and 0<=v<count and math.isfinite(w) and 0<=w<=1,'skin_weight',str(row))
            if w:weights[v][bone]=weights[v].get(bone,0)+w
    for v,values in enumerate(weights):
        require(len(values)<=8 and abs(sum(values.values())-1)<1e-5,'skin_normalization',str(v))
    return [[v,b,w] for v,values in enumerate(weights) for b,w in sorted(values.items())]


def prepare(ir):
    finite(ir)
    records=ir['records']; blockers=[]
    def block(code,source,detail):blockers.append({'code':code,'source':source,'detail':detail})
    merged=[r for r in records if r.get('class')=='DAZMergedMesh']
    require(len(merged)==1,'merged_mesh','Expected exactly one neutral merged body')
    record=merged[0]; raw_mesh=record['mesh']; parameters=record['parameters']
    target_id=str(parameters['targetMesh']['m_PathID']); graft_id=str(parameters['graftMesh']['m_PathID'])
    target=next(r for r in records if r.get('kind')=='unity_mesh' and r['object']==target_id)
    graft=next(r for r in records if r.get('kind')=='unity_mesh' and r['object']==graft_id)
    def evaluate(values):
        mesh=copy.deepcopy(raw_mesh)
        for selected, morph in values:
            offset=selected.get('vertex_offset',0)
            domain=graft['mesh'] if offset else target['mesh']
            apply_morph(mesh,morph,selected['value'],len(domain['vertices']),len(domain['uv']),offset)
        apply_graft_boundary(mesh,target['mesh'],parameters,graft['parameters'])
        return mesh
    morphs={r['id']:r for r in records if r.get('kind')=='morph'}
    selected=[]; formulas=[]
    for application in ir['applied_morphs']:
        require(application['id'] in morphs,'morph_source',application['id'])
        source=morphs[application['id']];selected.append((application,source['data']))
        for formula in source['data']['parameters'].get('formulas',[]):
            formulas.append({'source_id':source['id'],'path':source['path'],'formula':formula})
    if formulas:
        block('formula_adapter_pending',record['object'],f'{len(formulas)} formulas require bone/parameter evaluation; not vertex-only MorphTargets')
    if parameters.get('useGraftSymmetry'):
        block('nonlinear_graft_pending',record['object'],'Shape-dependent symmetry factors require nonlinear evaluation, not additive MorphTargets')
    x0=evaluate([]); xp0=evaluate(selected)
    body=preview_mesh(x0,'Neutral body',{'object':record['object']})
    body['normals']=normals_for_ue(body)
    for section in body['sections']:
        for i in range(0,len(section),3):section[i+1],section[i+2]=section[i+2],section[i+1]
    bones=reference_bones(ir['skeleton'])
    if sum(not b['parent'] for b in ir['skeleton'])>1:
        block('source_hierarchy_pending',record['object'],
              'DAZ parentBone has multiple roots. Actual Transform parent chain is not retained in Stage03 IR; identity assembly root is provisional, not verified binding.')
    skins=[r for r in records if r.get('kind')=='skin' and str(r['parameters']['dazMesh']['m_PathID'])==record['object']]
    require(len(skins)==1,'body_skin','No unique merged body skin')
    raw_skin=skins[0]['parameters']; influences=[]
    try:influences=general_weights(raw_skin,len(body['vertices']),bones)
    except Exception as exc:block(getattr(exc,'code','skin_error'),skins[0]['object'],str(exc))
    deltas=[]
    for application,morph in selected:
        isolated=evaluate([(dict(application,value=1.),morph)])
        base_delta=[[a-b for a,b in zip(v,base)] for v,base in zip(isolated['vertices'],x0['vertices'])]
        meta=morph['parameters']
        deltas.append({'source_id':application['id'],'name':'M_'+application['id'][:24],
                       'default':application['value'],'minimum':meta.get('min',0),'maximum':meta.get('max',1),
                       'deltas':[to_ue(base_delta[v]) for v in body['converted_to_source_vertex']],
                       'source_parameters':meta,'raw_deltas':morph['deltas'],'vertex_offset':application.get('vertex_offset',0)})
    # Record agreement against Stage03's evaluated logic, including graft boundary transfer.
    maximum=0.
    for v,source_v in enumerate(body['converted_to_source_vertex']):
        expected=to_ue(xp0['vertices'][source_v])
        actual=[body['vertices'][v][axis]+sum(m['default']*m['deltas'][v][axis] for m in deltas) for axis in range(3)]
        maximum=max(maximum,math.dist(actual,expected))
    if maximum>1e-4:block('nonlinear_shape',record['object'],f'X0 + p0*d differs from source by {maximum} cm')
    # No calibration claims: source raw TriAx/bulge and formulas remain in the immutable IR.
    return {'schema':'vam-native-source/1','status':'blocked' if blockers else 'source_ready',
            'decode_id':ir['decode_id'],'plan_id':ir['plan_id'],'shape_convention':'neutral_plus_parameters',
            'body':body,'bones':bones,'bind_signature':digest(bones),'influences':influences,
            'morphs':deltas,'formulas':formulas,'p0_vertex_error_cm':maximum,'blockers':blockers,
            'skin':{'source_object':skins[0]['object'],'use_general':bool(raw_skin['_useGeneralWeights']),
                    'has_general':bool(raw_skin['_hasGeneralWeights']),'calibration':'not_available' if not influences else 'source_general'},
            'source_errors':ir.get('errors',[]),
            'parts_status':'pending_skinwrap_influence_transfer',
            'hair_status':'source_only_stage10_groom_pending',
            'surface_status':'partial',
            'formal_assets_committed':False}


def prepare_file(path, destination):
    path=Path(path); original=path.read_bytes(); ir=json.loads(original)
    result=prepare(ir)
    result['source_ir']={'path':str(path.resolve()),'sha256':hashlib.sha256(original).hexdigest()}
    destination=Path(destination);destination.mkdir(parents=True,exist_ok=True)
    output=destination/(ir['decode_id']+'.native-source.json')
    output.write_text(json.dumps(result,ensure_ascii=False,separators=(',',':'),allow_nan=False),encoding='utf8')
    summary={k:v for k,v in result.items() if k not in ('body','bones','influences','morphs','formulas')}
    summary['counts']={'vertices':len(result['body']['vertices']),'bones':len(result['bones']),
                       'morphs':len(result['morphs']),'formulas':len(result['formulas'])}
    (destination/'latest-report.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf8')
    return result, output
