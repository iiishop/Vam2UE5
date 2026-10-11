"""C1: audit the exported actual asset; wide angles validate the existing fit, never refit."""
import json, math, hashlib, argparse
from pathlib import Path
import numpy as np
from vam_triax_lbs import axis_response

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--root',default='Saved/LowerBodyContact/C1');a=ap.parse_args();root=Path(a.root)
    if root.resolve()==(Path(__file__).resolve().parents[1]/'Saved/LowerBodyContact/C1').resolve() and (root.parent/'C1.5/latest-run.txt').exists():
        raise RuntimeError('Historical C1 is frozen; select an independent output root')
    c=json.loads((root/'native_contract_json.json').read_text(encoding='utf-8-sig'))['contract']
    ir=json.loads((root/'source_ir_json.json').read_text(encoding='utf-8-sig'))
    morphs={m['source_id']:m for m in c['morphs']};trace=[]
    for block in c['formula_classification']:
        m=morphs.get(block['source_id'],{})
        for f in block['retained_unexecuted_or_missing_targets']:
            trace.append(dict(source_id=block['source_id'],morph=m.get('display_name',m.get('name')),appearance_value=m.get('default'),classification=f['code'],target=f['target'],target_type=f['target_type'],formula=f['formula'],evidence='retained by actual contract; runtime contribution not yet proven',repair_status='candidate_only'))
    (root/'formula-trace.json').write_text(json.dumps(trace,ensure_ascii=False,indent=2),encoding='utf-8')
    bones=c['bones'];n=len(c['body']['vertices']);lookup={b['name']:i for i,b in enumerate(bones)}
    x=np.asarray(c['body']['vertices'],float)
    for m in c['morphs']:x+=float(m['default'])*np.asarray(m['deltas'])
    desc=np.eye(len(bones));w=np.zeros((n,len(bones)))
    for i,b in enumerate(bones):
        p=b['parent']
        while p>=0:desc[p,i]=1;p=bones[p]['parent']
    for v,b,val in c['influences']:w[int(v),int(b)]+=val
    cumulative=w@desc.T
    raw=next(r['parameters'] for r in ir['records'] if r.get('kind')=='skin' and r['object']==c['skin']['source_object'])
    rows=[]
    for node in raw['nodes']:
        if node['name'] not in ('lThigh','rThigh','lShin','rShin','pelvis','hip'):continue
        bi=lookup[node['name']];bind=np.asarray(bones[bi]['world_bind']);local=(x-bind[:3,3])@bind[:3,:3]
        wt=np.zeros((n,3));left=wt.copy();right=wt.copy();full=np.zeros(n,bool)
        for r in node['weights']:
            v=r['vertex']
            for sa,axis in enumerate((1,2,0)):
                key='xyz'[sa];wt[v,axis]=r[key+'weight'];left[v,axis]=r[key+'leftbulge'];right[v,axis]=r[key+'rightbulge']
        for v in node['fullyWeightedVertices']:wt[v,:]=1;full[v]=True
        active=np.any(wt>0,axis=1)
        if not active.any():continue
        for sa,axis in enumerate((1,2,0)):
            factors={s+t:node['bulgeFactors'].get('xyz'[sa]+s+t,0) for s in ('pos','neg') for t in ('left','right')}
            for deg in (-120,-110,-90,-60,-30,-22,22,30,60,90,110,120):
                ang=math.radians(deg);rigid=axis_response(local,axis,ang,np.ones(n),np.zeros(n),np.zeros(n),{},0)
                target=axis_response(local,axis,ang,wt[:,axis],left[:,axis],right[:,axis],factors,float(raw.get('bulgeScale',1)));target[full]=rigid[full]
                e=np.linalg.norm(rigid*cumulative[:,bi,None]-target,axis=1)
                ids=np.flatnonzero(active);ea=e[active];idx=int(ids[np.argmax(ea)])
                rows.append(dict(joint=node['name'],source_axis='xyz'[sa],degrees=deg,count=len(ids),rms_cm=float(np.sqrt(np.mean(ea**2))),p95_cm=float(np.percentile(ea,95)),maximum_cm=float(ea.max()),maximum_vertex=idx,scope='isolated-axis mathematical CPU port; not VaM runtime surface'))
    (root/'wide-angle-existing-fit.json').write_text(json.dumps(rows,indent=2),encoding='utf-8')
    binds=np.array([b['world_bind'] for b in bones]);invs=np.array([b['inverse_bind'] for b in bones])
    report=dict(contract_id=c['contract_id'],source_ir=c['source_ir'],calibration=c['skin'],axis_transform='Unity metres (x,y,z) -> UE centimetres (100*z,100*x,100*y), proper rotation det +1',bind_inverse_max_abs=float(np.max(np.abs(binds@invs-np.eye(4)))),reference_modes=sorted(set(b['reference_orientation_mode'] for b in bones)),pose_rotation_orders=sorted(set(b['pose_rotation_order'] for b in bones)),retained_formula_count=len(trace),active_retained_formula_count=sum(abs(t['appearance_value'] or 0)>1e-8 for t in trace),runtime_status='pending independent capture acceptance',proven_repair_list=[],wide_angle_scope='Existing weights only. Isolated all-axis stress probes; extreme twist is not an anatomical target. No refitting, no compound runtime equivalence claim.')
    (root/'contract-audit.json').write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps({k:report[k] for k in ('contract_id','bind_inverse_max_abs','retained_formula_count','active_retained_formula_count')}))
    print('wide_angle_rows',len(rows))
if __name__=='__main__':main()
