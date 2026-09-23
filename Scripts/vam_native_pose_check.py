"""Quantify transferred clothing LBS vs static SkinWrap of the posed LBS body.
This measures adapter discrepancy; neither side is a cloth physics simulation.
"""
import json,math,sys,os
from pathlib import Path
SCRIPTS=Path(__file__).resolve().parent
sys.path[:0]=[str(SCRIPTS),str(SCRIPTS.parent/'Saved/Python')]
import numpy as np
from vam_fit import fit_wrap,resolve_preview_wrap,preview_wrap_settings
from vam_decode import preview_mesh


def run(data):
    from vam_native_input import load_preview
    p=load_preview(data)
    ir=json.loads((data/'Decoded'/(p['decode_id']+'.ir.json')).read_text(encoding='utf8'))
    plan=json.loads((data/'Plans'/(ir['plan_id']+'.json')).read_text(encoding='utf8'))
    contract=json.loads((data/'NativeBuild'/(ir['decode_id']+'.calibrated.json')).read_text(encoding='utf8'))
    parts=json.loads((data/'NativeBuild'/(ir['decode_id']+'.parts.json')).read_text(encoding='utf8'))
    bones=contract['bones'];names={b['name']:i for i,b in enumerate(bones)}
    body=np.asarray(contract['body']['vertices'])
    for morph in contract['morphs']:body+=morph['default']*np.asarray(morph['deltas'])
    bw=np.zeros((len(body),len(bones)))
    for v,b,w in contract['influences']:bw[v,b]=w
    merged=next(r for r in ir['records'] if r.get('class')=='DAZMergedMesh')
    original=next(r['mesh'] for r in ir['records'] if r.get('kind')=='unity_mesh' and r['object']==str(merged['parameters']['targetMesh']['m_PathID']))
    results=[]
    for part in parts['parts']:
        record=next(r for r in ir['records'] if r.get('id')==part['source_id']);d=record['data']
        wrap,_=resolve_preview_wrap(d['meshes'],d['wraps'])
        settings,_=preview_wrap_settings(d,[(rid,plan['documents'][rid]['parameters']) for rid in plan['roots']])
        points=np.asarray(part['mesh']['vertices'])
        for m,param in zip(part['morphs'],contract['morphs']):points+=param['default']*np.asarray(m['deltas'])
        pw=np.zeros((len(points),len(bones)))
        for v,b,w in part['influences']:pw[v,b]=w
        errors=[];samples=[]
        for joint in ('lShldr','rShldr','lThigh','rThigh'):
            if joint not in names:continue
            bi=names[joint];desc=[]
            for i,b in enumerate(bones):
                j=i
                while j>=0 and j!=bi:j=bones[j]['parent']
                if j==bi:desc.append(i)
            bind=np.asarray(bones[bi]['world_bind']);q=bind[:3,:3];center=bind[:3,3]
            for degrees in (-22,22):
                a=math.radians(degrees);c=math.cos(a);s=math.sin(a)
                rot=q@np.array([[c,-s,0],[s,c,0],[0,0,1]])@q.T
                def deform(v,w):return v+((v-center)@rot.T+center-v)*w[:,desc].sum(axis=1)[:,None]
                posed_body=deform(body,bw);posed_part=deform(points,pw)
                verts=[[float(v[1]/100),float(v[2]/100),float(v[0]/100)] for v in posed_body]
                wrapped=None
                for target in (merged['mesh'],original):
                    try:
                        wrapped=preview_mesh(fit_wrap(d['meshes'][0],wrap,dict(target,vertices=verts[:len(target['vertices'])]),**settings),'',{})
                        break
                    except Exception:pass
                assert wrapped is not None
                e=np.linalg.norm(posed_part-np.asarray(wrapped['vertices']),axis=1)
                errors.extend(e.tolist());samples.append({'joint':joint,'local_axis':'UE Z','degrees':degrees,'maximum_cm':float(e.max())})
        e=np.asarray(errors)
        results.append({'path':part['path'],'rms_cm':float(np.sqrt(np.mean(e*e))),'maximum_cm':float(e.max()),'samples':samples})
    report={'decode_id':ir['decode_id'],'reference':'SkinWrap of posed LBS body vs transferred part LBS; not VaM runtime truth or physics','parts':results}
    output=Path(os.environ.get('VAM_POSE_REPORT_FILE',str(data/'NativeBuild/clothing-pose-errors.json')))
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf8')
    print(json.dumps([(r['path'].rsplit('/',1)[-1],r['rms_cm'],r['maximum_cm']) for r in results]))

if __name__=='__main__':run(SCRIPTS.parent/'Saved')
