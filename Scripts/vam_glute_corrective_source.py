"""Read-only, reference-checked source corrective recovery for immutable runtime builds.
No VaM code executes. Missing source is explicit procedural fallback, never a guessed driver.
"""
import hashlib, json, sys
from pathlib import Path

def recover(ir, settings_file, family):
    if 'glute_corrective' not in family:
        raise ValueError('Unsupported family: no Glute corrective policy')
    captured=next((r['data'] for r in ir.get('records',[]) if r.get('kind')=='glute_pose_corrective'),None)
    retained=family.get('glute_corrective_source')
    for cached in (captured,retained):
        if cached and cached.get('version')==1 and cached.get('target')==family['glute_corrective'].get('source_target') and cached.get('source_hashes') and all(ir.get('source_hashes',{}).get(k)==v for k,v in cached['source_hashes'].items()):
            return cached
    result=dict(version=1,mode='procedural',reason='No verified source driver/delta chain available',deltas=[])
    settings=Path(settings_file)
    if not settings.exists():return result
    root=Path(json.loads(settings.read_text(encoding='utf-8')).get('root',''))
    result=extract(root,ir.get('source_hashes',{}),family,result)
    if result['deltas']:
        mesh=next((r['mesh'] for r in ir.get('records',[]) if r.get('class')=='DAZMergedMesh'),None)
        if mesh: result['neutral_vertices_cm']=[[v[2]*100,v[0]*100,v[1]*100] for v in mesh['vertices']]
    return result

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
            if d['morph1Low']!=0 or d['morph1High']!=1 or not d['clampMorphValue']:continue
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
