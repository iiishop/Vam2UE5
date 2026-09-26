"""Compare actual old/new official eye geometry over one shared topology ROI."""
import argparse,json
from pathlib import Path
import numpy as np
from vam_face_eye_intersections import crossings
from vam_face_phong import PhongSurface
from vam_face_surface import normals


def report(job,previous):
    job=Path(job);previous=Path(previous)
    read=lambda p:json.loads(Path(p).read_text(encoding='utf8'))
    regions=read(job/'eye-surface-ownership.json');correspondence=read(job/'correspondence.json')['candidates']
    roi=set(sum([q['target_vertices'] for q in regions],[]))
    for curve in correspondence:
        if curve['semantic_id'].startswith('eyelid'):roi.update(curve['target_vertex_ids'])
    source=read(job/'source.json');sv=np.asarray(source['vertices']);st=np.asarray(source['triangles']).reshape(-1,3)
    face=st[np.isin(source['triangle_materials'],('Face','Head','Ears','Lips','Nostrils'))]
    points=sv[np.unique(face)];origin=points.mean(0);scale=2*np.sqrt(np.mean(np.sum((points-origin)**2,axis=1)))
    exterior=next(q for q in regions if q['region']=='exterior');ids=np.asarray(exterior['target_vertices'])
    surface=PhongSurface((sv-origin)/scale,np.asarray(exterior['source_triangles']))
    result={'scope':'same geodesic eye topology ROI; actual official neutral head exports',
            'source_semantics_verified':False,'visual_acceptance_passed':False,'roi_vertices':len(roi)}
    for name,root in [('previous',previous),('new',job)]:
        export=root/'Official/RigReload/actual-head.json'
        if not export.exists():raise ValueError('ActualPostRigEyeExportRequired')
        mesh=read(export);pose=read(root/'pose-transform.json');rotation=np.asarray(pose['posed_to_apose_rotation'])
        v=(np.asarray(mesh['vertices'])-pose['apose_center'])@rotation.T+pose['posed_center']
        triangles=np.asarray(mesh['triangles']).reshape(-1,3)
        tri_ids=np.flatnonzero(np.any(np.isin(triangles,list(roi)),axis=1))
        pairs=crossings(v,triangles[tri_ids],triangles,True)
        unique=sorted({tuple(sorted((int(tri_ids[a]),b))) for a,b in pairs})
        _,ns,d=surface.query((v[ids]-origin)/scale);vn,_=normals(v,triangles)
        result[name]={'export':str(export),'unique_skin_crossing_pairs':len(unique),'crossing_pairs':unique,
                      'exterior_surface_mean_mm':float(d.mean()*scale*10),
                      'exterior_surface_max_mm':float(d.max()*scale*10),
                      'exterior_normal_one_minus_cosine':float(np.mean(1-np.clip(np.sum(vn[ids]*ns,axis=1),-1,1)))}
    result['discrete_skin_crossing_gate_passed']=result['new']['unique_skin_crossing_pairs']==0
    (job/'eye-repair-report.json').write_text(json.dumps(result,indent=2),encoding='utf8')
    return result


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--job',required=True);p.add_argument('--previous',required=True)
    a=p.parse_args();r=report(a.job,a.previous)
    print(json.dumps({k:{x:y for x,y in v.items() if x!='crossing_pairs'} if isinstance(v,dict) else v for k,v in r.items()}))
