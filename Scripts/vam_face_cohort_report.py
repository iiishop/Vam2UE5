"""Measure where structural mismatches enter a multi-preset conversion.

Reports frozen fit support and provisional annulus identity, not visual approval.
No solve is performed and no gate is relaxed for production by this diagnostic.
"""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from vam_face_adaptive import AdaptiveFit
from vam_face_fidelity import VIEWS,view_matrix
from vam_face_fidelity_views import render
from vam_face_eye_intersections import crossings
from vam_face_topology_equivalence import with_official_quads
from vam_metahuman import write_json

SETTINGS=dict(surface_interpolation='phong',boundary_constraints='material-and-annulus',strain_model='arap',normalization='rms',
    eyelid_correspondence='ordered-cycle',eye_clearance_guard=True,eye_surface_ownership=True,eye_tangent_refinement=True,
    eye_neighborhood='geodesic',eye_curve_interpolation='phong',eye_sampling_regularization=True)


def read(p):return json.loads(Path(p).read_text(encoding='utf8'))


def report(request):
    r=read(request);root=Path(r['output']);engine=Path(r['engine'])/'Engine'
    lm=read(engine/'Plugins/MetaHuman/MetaHumanCharacter/Content/Face/IdentityTemplate/face_landmarks.json')
    quad=engine/'Plugins/MetaHuman/MetaHumanAnimator/Content/MeshFitting/Template/mean.obj'
    source=read(root/'Initial/target.json');sv=np.asarray(source['vertices']);st=np.asarray(source['triangles']).reshape(-1,3)
    face=st[np.isin(source['triangle_materials'],('Face','Head','Ears','Lips','Nostrils'))]
    points=sv[np.unique(face)];center=(points.max(0)+points.min(0))/2;radius=float(np.linalg.norm(points-center,axis=1).max())
    stages=[('SOURCE',sv,st)];reports={};eye_reference=None
    for stage in ('Coarse','Tracked','OfficialBaseline','OfficialTemplate','Final'):
        path=root/stage/'head.json'
        if stage in ('OfficialTemplate','Final'):
            exp=root/'Pipeline/Experiment';actual=exp/'Official'/('TemplateReload' if stage=='OfficialTemplate' else 'RigReload')/'actual-head.json'
            if not actual.exists():continue
            h=read(actual);pose=read(exp/'pose-transform.json');rot=np.asarray(pose['posed_to_apose_rotation'])
            h['vertices']=((np.asarray(h['vertices'])-pose['apose_center'])@rot.T+pose['posed_center']).tolist()
        elif stage=='OfficialBaseline':
            actual=root/'BaselineReload/actual-head.json'
            if not actual.exists():continue
            h=read(actual);initial=read(root/'Tracked/head.json');a=np.asarray(initial['apose_vertices']);b=np.asarray(initial['vertices'])
            ac=a.mean(0);bc=b.mean(0);u,_,vt=np.linalg.svd((a-ac).T@(b-bc));rotation=u@vt
            if np.linalg.norm((a-ac)@rotation+bc-b,axis=1).max()>1e-3:raise ValueError('BaselinePoseNotRigid')
            h['vertices']=((np.asarray(h['vertices'])-ac)@rotation+bc).tolist()
        elif path.exists():h=read(path)
        else:continue
        v=np.asarray(h['vertices']);t=np.asarray(h['triangles']).reshape(-1,3);stages.append((stage,v,t))
        if stage=='Tracked':eye_reference=v
        diagnostic_source=dict(source,**SETTINGS,eye_self_intersection_guard=False)
        try:
            support_head=read(root/'Tracked/head.json') if stage in ('OfficialBaseline','OfficialTemplate','Final') else h
            fit=AdaptiveFit(diagnostic_source,with_official_quads(support_head,quad),lm)
            regions=[];roi=set(fit.eye_boundary_ids)
            for region in fit.eye_region_diagnostics:
                ids=np.asarray(region['target_vertices']);roi.update(ids.tolist());fixed=np.intersect1d(ids,fit.fixed)
                regions.append({'region':region['region'],'vertices':len(ids),'fixed_count':len(fixed),
                    'fixed_vertex_ids':fixed.tolist(),'surface_objective_count':len(np.intersect1d(ids,fit.surface_ids))})
            ti=np.flatnonzero(np.any(np.isin(t,list(roi)),axis=1));pairs=crossings(v,t[ti],t,True)
            unique={tuple(sorted((int(ti[a]),b))) for a,b in pairs}
            region_by_vertex={i:region['region'] for region in fit.eye_region_diagnostics for i in region['target_vertices']}
            crossing_owners={}
            for a,b in unique:
                owners={region_by_vertex.get(int(i),'rim_or_outside') for i in np.r_[t[a],t[b]]}
                key='+'.join(sorted(owners));crossing_owners[key]=crossing_owners.get(key,0)+1
            curves={}
            for name,entry in lm.items():
                if not any(word in name for word in ('eye','nasolabial','nostril')):continue
                ids=np.asarray(entry.get('vIDs',[entry['vID']] if 'vID' in entry else []),int)
                if not len(ids):continue
                curves[name]={'vertices':len(ids),'fixed_count':len(np.intersect1d(ids,fit.fixed)),
                    'surface_objective_count':len(np.intersect1d(ids,fit.surface_ids))}
            reports[stage]={'eye_skin_crossing_pairs':len(unique),'crossing_triangle_pairs':sorted(unique),
                'crossing_topological_ownership':crossing_owners,'frozen_support':regions,'official_curve_support':curves,
                'support_reference':'Tracked initial input' if stage in ('OfficialBaseline','OfficialTemplate','Final') else stage+' initial input',
                'correspondence':fit.correspondence,'scope':'crossing measured with guard disabled only in read-only diagnostic; not an acceptance override'}
        except ValueError as exc:reports[stage]={'diagnostic_error':str(exc)}
    size=384;sheet=Image.new('RGB',(size*len(stages),size*len(VIEWS)))
    for row,view in enumerate(VIEWS):
        for col,(label,v,t) in enumerate(stages):
            tile=render(v,t,view_matrix(*view),center,radius,size)
            ImageDraw.Draw(tile).text((8,8),f'{label} yaw={view[0]} pitch={view[1]}',fill='white');sheet.paste(tile,(col*size,row*size))
    sheet.save(root/'structure-seven-views.png')
    if eye_reference is not None:
        eye=Image.new('RGB',(size*len(stages),size*6));row=0
        for side in ('l','r'):
            ids=lm['crv_eyelid_upper_'+side]['vIDs']+lm['crv_eyelid_lower_'+side]['vIDs'];q=eye_reference[ids];c=q.mean(0);rad=float(np.linalg.norm(q-c,axis=1).max()*1.6)
            for yaw in (0,-45,45):
                for col,(label,v,t) in enumerate(stages):
                    tile=render(v,t,view_matrix(yaw,0),c,rad,size);ImageDraw.Draw(tile).text((8,8),f'{label} {side} yaw={yaw}',fill='white');eye.paste(tile,(col*size,row*size))
                row+=1
        eye.save(root/'structure-eyes.png')
    write_json(root/'structure-report.json',{'preset':r['preset'],'stages':reports,'settings':SETTINGS,
        'source_topology':source['source_base_topology_sha256'],'stage_order':[s[0] for s in stages],
        'camera':{'center':center.tolist(),'radius':radius,'views':VIEWS},'source_semantics_verified':False,'visual_acceptance_passed':False})
    print(json.dumps({'preset':r['preset'],'stages':{k:{x:y for x,y in v.items() if x in ('eye_skin_crossing_pairs','diagnostic_error')} for k,v in reports.items()}}),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True);a=p.parse_args();report(a.request)
