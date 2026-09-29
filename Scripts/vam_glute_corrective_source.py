"""Read-only, reference-checked source corrective recovery for immutable runtime builds.
No VaM code executes. Missing source is explicit procedural fallback, never a guessed driver.
"""
import copy, hashlib, json, sys
from pathlib import Path

def recover(ir, settings_file, family):
    if 'glute_corrective' not in family:
        raise ValueError('Unsupported family: no Glute corrective policy')
    captured=next((r['data'] for r in ir.get('records',[]) if r.get('kind')=='glute_pose_corrective'),None)
    retained=family.get('glute_corrective_source')
    for cached in (captured,retained):
        if cached and cached.get('version') in (1,2) and cached.get('target')==family['glute_corrective'].get('source_target') and cached.get('source_hashes') and all(ir.get('source_hashes',{}).get(k)==v for k,v in cached['source_hashes'].items()):
            return map_to_merged(ir, cached, family)
    result=dict(version=1,mode='procedural',reason='No verified source driver/delta chain available',deltas=[])
    settings=Path(settings_file)
    if not settings.exists():return result
    root=Path(json.loads(settings.read_text(encoding='utf-8')).get('root',''))
    result=extract(root,ir.get('source_hashes',{}),family,result)
    return map_to_merged(ir,result,family) if result['deltas'] else result

def extract(root, hashes, family, result=None):
    result=result or dict(version=1,mode='procedural',reason='Source chain unavailable',deltas=[])
    paths={f:Path(root)/'VaM_Data/StreamingAssets'/f for f in ('a_per','f_mb')}
    for f,p in paths.items():
        if not p.is_file():result['reason']='Configured source library unavailable';return result
        actual=hashlib.sha256(p.read_bytes()).hexdigest()
        if hashes.get('VaM_Data/StreamingAssets/'+f)!=actual:
            result['reason']='Source bundle digest differs from persisted source IR';return result
    sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'Saved/Python'))
    from vam_unity import Bundle
    try:
        person=Bundle(paths['a_per']);bank=Bundle(paths['f_mb'])
        bones={o.path_id:d['_id'] for o,c,d in person.records({'DAZBone'})}
        semantics=family['glute_structure'];expected={semantics['left_thigh'],semantics['right_thigh']}
        drivers=[]
        for o in person.env.objects:
            if o.type.name!='MonoBehaviour' or person.class_name(o)!='SetDAZMorphFromAverageBoneAngle':continue
            d=o.read_typetree()
            if {bones.get(d['dazBone1']['m_PathID']),bones.get(d['dazBone2']['m_PathID'])}!=expected:continue
            if d['angleAxis1']!=0 or d['angleAxis2']!=0 or d['angleLow']!=0 or d['angleHigh']!=-100:continue
            if d['morph1Low']!=0 or d['morph1High']!=1 or not d['clampMorphValue'] or d.get('_multiplier',1)!=1:continue
            # The family mapping identifies the verified target; names alone never establish a driver.
            if d['morph1Name']!=family['glute_corrective'].get('source_target'):continue
            drivers.append((o.path_id,d))
        if len(drivers)!=1:result['reason']='No unique supported thigh-average bend driver';return result
        driver_id,driver=drivers[0];matches=[]
        for o,c,d in bank.records({'DAZMorphSubBank'}):
            for m in d['_morphs']:
                if m['morphName']==driver['morph1Name'] and m['numDeltas']==len(m['deltas']) and m['deltas']:
                    matches.append((o.path_id,m))
        if len(matches)!=1:result['reason']='Driver target morph is missing or ambiguous';return result
        bank_id,morph=matches[0]
        result.update(mode='source_delta_plus_procedural',reason='Verified thigh-average driver to target morph to source vertex deltas; localized and pose-space adapted, not exact VaM runtime reproduction',driver_object=str(driver_id),bank_object=str(bank_id),target=morph['morphName'],driver=driver,
                      source_hashes={k:v for k,v in hashes.items() if k.endswith(('/a_per','/f_mb'))},
                      deltas=[[v['vertex'],100*v['delta']['z'],100*v['delta']['x'],100*v['delta']['y']] for v in morph['deltas']])
    except (ValueError,KeyError,ImportError) as error:
        result['reason']='Source adapter rejected chain: '+str(error)
    return result


def map_to_merged(ir, source, family):
    """Replay the existing verified Boundary graft operator on a unit bend delta.

    Source bank vertex IDs belong to the target mesh; many are hidden by the graft.
    Applying only those IDs to the merged mesh silently loses the visible response.
    Keep the original 266-row bank data separately from the transferred field.
    """
    from vam_fit import apply_graft_boundary
    records=ir.get('records',[])
    merged=next((r for r in records if r.get('class')=='DAZMergedMesh'),None)
    if merged is None:
        raise ValueError('Verified bend requires persisted merged/target/graft topology')
    params=merged['parameters']
    if params.get('useGraftSymmetry') or params.get('hasGraft2'):
        raise ValueError('Corrective source mapping requires supported linear single Boundary graft')
    target=next(r for r in records if r.get('kind')=='unity_mesh' and r['object']==str(params['targetMesh']['m_PathID']))
    graft=next(r for r in records if r.get('kind')=='unity_mesh' and r['object']==str(params['graftMesh']['m_PathID']))
    raw=source.get('raw_deltas',source['deltas'])
    base=copy.deepcopy(merged['mesh']);changed=copy.deepcopy(base)
    for v,x,y,z in raw:
        if not 0<=v<len(target['mesh']['vertices']):raise ValueError('Bend delta is outside target base domain')
        for a,d in enumerate((y/100,z/100,x/100)):changed['vertices'][v][a]+=d
    evidence=apply_graft_boundary(base,target['mesh'],params,graft['parameters'])
    apply_graft_boundary(changed,target['mesh'],params,graft['parameters'])
    deltas=[]
    for v,(a,b) in enumerate(zip(base['vertices'],changed['vertices'])):
        d=[(b[k]-a[k])*100 for k in (2,0,1)]
        if sum(x*x for x in d)>1e-20:deltas.append([v,*d])
    visible={v for p in merged['mesh']['polygons'] for v in p['vertices']}
    skin=next(r['parameters'] for r in records if r.get('kind')=='skin' and str(r['parameters']['dazMesh']['m_PathID'])==merged['object'])
    if skin['_useGeneralWeights']:raise ValueError('Bilateral reference currently requires verified TriAx source')
    names={family['glute_structure']['left_thigh'],family['glute_structure']['right_thigh']}
    # Preserve source node order, including disjoint graft domains of the same bone.
    nodes=[copy.deepcopy(n) for n in skin['nodes'] if n['name'] in names]
    result=dict(source,version=2,raw_deltas=raw,deltas=deltas,
        neutral_vertices_cm=[[v[2]*100,v[0]*100,v[1]*100] for v in base['vertices']],
        topology_mapping=dict(replay='source Boundary graft replay',target=target['object'],graft=graft['object'],
            raw_count=len(raw),mapped_count=len(deltas),raw_visible_count=sum(v in visible for v,*_ in raw),
            mapped_visible_count=sum(v in visible for v,*_ in deltas),**evidence),
        source_skin_nodes=nodes,source_bulge_scale=skin['bulgeScale'],
        reference_scope='DAZSkinV2 CPU bilateral isolated thigh-X equation replay before post-skin smoothing/physics; not captured VaM render truth')
    return result



def source_driver_weight(left_source_x_degrees, right_source_x_degrees, driver):
    low, high = driver['angleLow'], driver['angleHigh']
    if low == high: raise ValueError('Degenerate source driver interval')
    angle = (left_source_x_degrees + right_source_x_degrees) * .5 * driver.get('_multiplier', 1.)
    t = (angle-low)/(high-low)
    value = driver['morph1Low'] + t*(driver['morph1High']-driver['morph1Low'])
    return min(max(value, min(driver['morph1Low'], driver['morph1High'])), max(driver['morph1Low'], driver['morph1High'])) if driver['clampMorphValue'] else value
