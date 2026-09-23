"""SkinWrap-correspondence transfer; static reference, not cloth simulation."""
import copy
import math
from pathlib import Path
import sys
sys.path[:0]=[str(Path(__file__).resolve().parent),str(Path(__file__).resolve().parents[1]/'Saved/Python')]
import numpy as np
from vam_decode import preview_mesh,require
from vam_fit import fit_wrap,resolve_preview_wrap,preview_wrap_settings
from vam_ue_mesh import normals_for_ue


def prepare_parts(ir,plan,preview,contract):
    from vam_native_job_state import progress,check_cancel
    merged=next(r for r in ir['records'] if r.get('class')=='DAZMergedMesh')
    target=next(r['mesh'] for r in ir['records'] if r.get('kind')=='unity_mesh' and r['object']==str(merged['parameters']['targetMesh']['m_PathID']))
    body=contract['body'];uv_count=len(body['vertices']);bone_count=len(contract['bones'])
    weights=np.zeros((uv_count,bone_count))
    for v,b,w in contract['influences']:weights[v,b]=w
    neutral=np.asarray(body['vertices'])
    def source_mesh(points):
        result=copy.deepcopy(merged['mesh'])
        result['vertices']=[[float(p[1]/100),float(p[2]/100),float(p[0]/100)] for p in points[:len(result['vertices'])]]
        return result
    neutral_source=source_mesh(neutral);parts=[];missing=[]
    clothing=[r for r in ir['records'] if r.get('kind')=='dynamic' and r['path'].startswith('Custom/Clothing/')]
    progress('转移服装蒙皮','逐个核对 SkinWrap 对应',True,0,len(clothing))
    for part_number,record in enumerate(clothing,1):
        check_cancel()
        progress('转移服装蒙皮',record['path'],True,part_number-1,len(clothing))
        try:
            data=record['data'];mesh=data['meshes'][0];wrap,resolution=resolve_preview_wrap(data['meshes'],data['wraps'])
            settings,provenance=preview_wrap_settings(data,[(rid,plan['documents'][rid]['parameters']) for rid in plan['roots']])
            shape=None;binding_target=None
            for candidate in (neutral_source,dict(target,vertices=neutral_source['vertices'][:len(target['vertices'])])):
                try:shape=fit_wrap(mesh,wrap,candidate,**settings);binding_target=candidate;break
                except Exception:pass
            require(shape is not None,'part_wrap','No verified body target')
            rendered=preview_mesh(shape,record['path'].rsplit('/',1)[-1],{'source':record['source'],'path':record['path']})
            rendered['normals']=normals_for_ue(rendered)
            for section in rendered['sections']:
                for j in range(0,len(section),3):section[j+1],section[j+2]=section[j+2],section[j+1]
            base_weights=[];correspondence=[]
            for v,row in enumerate(wrap['vertices'][:len(mesh['vertices'])]):
                _,a,b,c,*_=row;A,B,C=neutral[[a,b,c]];P=np.array(rendered['vertices'][v])
                uv=np.linalg.lstsq(np.column_stack((B-A,C-A)),P-A,rcond=None)[0]
                bary=np.array([1-uv.sum(),*uv]);bounded=np.maximum(bary,0);bounded/=bounded.sum()
                mixed=bounded@weights[[a,b,c]];keep=np.argsort(-mixed,kind='stable')[:8]
                filtered=np.zeros(bone_count);filtered[keep]=mixed[keep];filtered/=filtered.sum()
                require(np.all(np.isfinite(filtered)),'part_weights',str(v))
                base_weights.append(filtered)
                correspondence.append({'triangle':int(row[0]),'body_vertices':[a,b,c],'projected_barycentric':bary.tolist(),'bounded_barycentric':bounded.tolist()})
            influences=[]
            for v,source in enumerate(rendered['converted_to_source_vertex']):
                influences.extend([v,int(b),float(w)] for b,w in enumerate(base_weights[source]) if w>1e-10)
            morphs=[]
            for morph in contract['morphs']:
                changed=source_mesh(neutral+np.asarray(morph['deltas']))
                candidate=dict(binding_target,vertices=changed['vertices'][:len(binding_target['vertices'])])
                fitted=preview_mesh(fit_wrap(mesh,wrap,candidate,**settings),'',{})
                morphs.append({'name':morph['name'],'deltas':(np.array(fitted['vertices'])-np.array(rendered['vertices'])).tolist()})
            p0=neutral.copy()
            for morph in contract['morphs']:p0+=morph['default']*np.asarray(morph['deltas'])
            changed=source_mesh(p0);candidate=dict(binding_target,vertices=changed['vertices'][:len(binding_target['vertices'])])
            actual=np.array(preview_mesh(fit_wrap(mesh,wrap,candidate,**settings),'',{})['vertices'])
            reconstructed=np.array(rendered['vertices'])
            for m,p in zip(morphs,contract['morphs']):reconstructed+=p['default']*np.asarray(m['deltas'])
            errors=np.linalg.norm(actual-reconstructed,axis=1)
            # Preserve exact appearance even when additive clothing Morphs are invalid.
            # Such parts remain skeletal but explicitly do not expose shape Morphs.
            nonlinear=float(errors.max())>.5
            neutral_vertices=rendered['vertices']
            if nonlinear:
                rendered=dict(rendered,vertices=actual.tolist())
                morphs=[]
            mi=next(i for i,m in enumerate(preview['meshes']) if m['locator'].get('source')==record['source'] and m['locator'].get('path')==record['path'])
            parts.append({'source_id':record['id'],'path':record['path'],'mesh':rendered,'influences':influences,
                          'morphs':morphs,'preview_mesh_index':mi,'correspondence':correspondence,'wrap_parameters':provenance,
                          'baked_appearance':nonlinear,'neutral_vertices':neutral_vertices if nonlinear else None,
                          'shape_limitation':'Exact p0 static wrap; nonlinear shape edits are not applied to this part' if nonlinear else None,
                          'p0_wrap_error_cm':{'rms':float(np.sqrt(np.mean(errors**2))),'max':float(errors.max())},
                          'pose_validation':'pending; transferred LBS is not per-frame SkinWrap or physics'})
        except Exception as exc:missing.append({'path':record['path'],'error':str(exc)})
    progress('转移服装蒙皮','服装对应处理完成',True,len(clothing),len(clothing))
    return {'adapter_version':3,'parts':parts,'missing':missing,'skin_transfer':'bounded projected triangle barycentric; at most 8 influences'}


if __name__=='__main__':
    import argparse,json
    parser=argparse.ArgumentParser();parser.add_argument('--data',type=Path,required=True);saved=parser.parse_args().data
    from vam_native_input import load_preview
    preview=load_preview(saved)
    ir=json.loads((saved/'Decoded'/(preview['decode_id']+'.ir.json')).read_text(encoding='utf8'))
    plan=json.loads((saved/'Plans'/(ir['plan_id']+'.json')).read_text(encoding='utf8'))
    contract=json.loads((saved/'NativeBuild'/(ir['decode_id']+'.calibrated.json')).read_text(encoding='utf8'))
    parts=prepare_parts(ir,plan,preview,contract);parts['contract_id']=contract['contract_id']
    (saved/'NativeBuild'/(ir['decode_id']+'.parts.json')).write_text(json.dumps(parts,ensure_ascii=False,separators=(',',':')),encoding='utf8')
    print(json.dumps({'parts':len(parts['parts']),'missing':parts['missing']},ensure_ascii=True))
