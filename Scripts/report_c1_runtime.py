"""Validate identity/stability before accepting C1 VaM runtime captures."""
import json, hashlib, html
from pathlib import Path
from collections import Counter
import numpy as np
ROOT=Path('Saved/LowerBodyContact/C1')
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def metric(v):
    v=np.asarray(v,float)
    return dict(rms_cm=float(np.sqrt(np.mean(v*v))),p95_cm=float(np.percentile(v,95)),maximum_cm=float(v.max()),maximum_vertex=int(v.argmax()))
def verts(path):return np.fromfile(path,dtype='<f4').reshape(-1,3).astype(float)
def main():
    r=ROOT/'runtime';c=read(ROOT/'native_contract_json.json')['contract'];loaded=read(r/'loaded-appearance.json');standing=read(r/'core-off-standing.json')
    posefiles=sorted(p for p in r.glob('core-off-*.json') if not p.stem.endswith('-repeat'))
    results=[];legids={'hip','pelvis','lThigh','rThigh','lShin','rShin','lFoot','rFoot'}
    for p in posefiles:
        pose=p.stem.removeprefix('core-off-');on=r/('core-on-'+pose+'.json');off=read(p)
        row={'pose':pose,'core_on_captured':on.exists()}
        repeat=r/(p.stem+'-repeat.json')
        if repeat.exists():
            rr=read(repeat);row['off_repeat_surface']=metric(np.linalg.norm(verts(r/off['smoothed']['file'])-verts(r/rr['smoothed']['file']),axis=1)*100)
        if on.exists():
            ondata=read(on);ob={b['id']:b for b in off['bones']};nb={b['id']:b for b in ondata['bones']}
            row['lowerbody_matrix_max_abs']=max(float(np.max(np.abs(np.array(ob[b]['worldMatrix'],float)-np.array(nb[b]['worldMatrix'],float)))) for b in legids if b in ob and b in nb)
            row['core_surface_difference']=metric(np.linalg.norm(verts(r/off['smoothed']['file'])-verts(r/ondata['smoothed']['file']),axis=1)*100)
            om={m['uid']:m for m in off['morphs']};nm={m['uid']:m for m in ondata['morphs']}
            for key in om.keys()|nm.keys():
                om.setdefault(key,{'morphValue':'0','drivenBy':''});nm.setdefault(key,{'morphValue':'0','drivenBy':''})
            row['changed_morphs']=[dict(uid=k,off=float(om[k]['morphValue']),on=float(nm[k]['morphValue']),drivenBy=nm[k]['drivenBy']) for k in om.keys()&nm.keys() if abs(float(om[k]['morphValue'])-float(nm[k]['morphValue']))>1e-6]
            row['accepted_same_lowerbody_pose']=row['lowerbody_matrix_max_abs']<1e-5
            rest=verts(r/(p.stem+'-morphed.f32')); lower=(rest[:,1]>0.25)&(rest[:,1]<1.15)
            for label,stem,base in [('off',p.stem,off),('on',on.stem,ondata)]:
                repeat_data=read(r/(stem+'-repeat.json'))
                repeat_err=np.linalg.norm(verts(r/base['smoothed']['file'])-verts(r/repeat_data['smoothed']['file']),axis=1)*100
                row[label+'_repeat_surface']=metric(repeat_err)
                row[label+'_repeat_lowerbody']=metric(repeat_err[lower])
            row['accepted_stable_surface']=max(row['off_repeat_lowerbody']['maximum_cm'],row['on_repeat_lowerbody']['maximum_cm'])<0.01
            row['accepted_stable_fullbody']=max(row['off_repeat_surface']['maximum_cm'],row['on_repeat_surface']['maximum_cm'])<0.01
        if (r/(p.stem+'-morphed.f32')).exists():
            input_v=verts(r/(p.stem+'-morphed.f32')); target=verts(r/off['raw']['file']); runtime_bones={b['id']:b for b in off['bones']}
            palette=[]; invalid_bones=[]
            for bone in c['bones']:
                b=runtime_bones[bone['name']]
                bind=np.array(b['morphedBind'],float).reshape(4,4)
                if abs(np.linalg.det(bind))<1e-10:
                    invalid_bones.append(len(palette));palette.append(np.eye(4))
                else:palette.append(np.array(b['worldMatrix'],float).reshape(4,4)@np.linalg.inv(bind))
            palette=np.array(palette); predicted=np.zeros_like(input_v)
            inf=np.array(c['influences']); vi=inf[:,0].astype(int); bi=inf[:,1].astype(int); weight=inf[:,2]
            pt=np.einsum('nij,nj->ni',palette[bi,:3,:3],input_v[vi])+palette[bi,:3,3]
            np.add.at(predicted,vi,pt*weight[:,None])
            err=np.linalg.norm(predicted-target,axis=1)*100
            invalid_vertices=np.unique(vi[np.isin(bi,invalid_bones)&(weight>1e-8)])
            valid=np.ones(len(err),bool);valid[invalid_vertices]=False
            row['excluded_noninvertible_bind_bones']=[c['bones'][i]['name'] for i in invalid_bones]
            row['excluded_noninvertible_bind_vertices']=invalid_vertices.tolist()
            row['runtime_input_actual_bind_source_lbs_vs_vam_raw']=metric(err[valid])
            row['runtime_input_actual_bind_source_lbs_vs_vam_raw']['maximum_vertex']=int(np.flatnonzero(valid)[np.argmax(err[valid])])
            mask=(input_v[:,1]>0.25)&(input_v[:,1]<1.15)&valid
            row['lower_body_source_lbs_vs_vam_raw']=metric(err[mask]); row['lower_body_source_lbs_vs_vam_raw']['maximum_vertex']=int(np.flatnonzero(mask)[np.argmax(err[mask])])
            row['lbs_scope']='Existing source contract fitted influences, actual VaM pre-skin input and actual runtime bind/current matrices. Not final UE helper-weight mesh.'
        results.append(row)
    shape=np.array(c['body']['vertices'],float)
    for m in c['morphs']:shape+=float(m['default'])*np.array(m['deltas'],float)
    va=verts(r/'core-off-standing-morphed.f32')[:,[2,0,1]]*100
    shape_metric=metric(np.linalg.norm(shape-va,axis=1)) if shape.shape==va.shape and standing.get('morphed_input_source')=='DAZCharacterRun.snappedMorphedUVVertices before skin' else {'rejected':'missing verified actual merged morph input; legacy DAZMesh buffer is stale for this pipeline'}
    cb={b['name']:b for b in c['bones']};bindings=[]
    C=np.array([[0,0,1],[1,0,0],[0,1,0]],float)
    for b in standing['bones']:
        if b['id'] not in cb:continue
        m=np.array(b['morphedBind'],float).reshape(4,4);ue=np.array(cb[b['id']]['world_bind'],float)
        q=C@m[:3,:3]@C.T
        angle=np.degrees(np.arccos(np.clip((np.trace(q.T@ue[:3,:3])-1)/2,-1,1)))
        bindings.append(dict(bone=b['id'],center_error_cm=float(np.linalg.norm(C@m[:3,3]*100-ue[:3,3])),orientation_error_deg=float(angle),runtime_orientation=b['morphedWorldOrientation'],runtime_offsets=b['morphOrientationOffsets'],runtime_rotation_formulas=b['morphRotations']))
    preset=read(Path(loaded['preset']));pm=next(s for s in preset['storables'] if s['id']=='geometry')['morphs'];lm={m['uid']:m for m in loaded['morphs']};identity=[]
    for m in pm:
        target=lm.get(m['uid']);identity.append(dict(uid=m['uid'],requested=float(m['value']),actual=None if target is None else float(target['morphValue']),matched=target is not None and abs(float(target['morphValue'])-float(m['value']))<1e-5))
    result=dict(contract_id=c['contract_id'],appearance_identity=identity,appearance_all_requested_match=all(x['matched'] for x in identity),source_shape_difference=shape_metric,source_shape_comparison_scope='Actual pre-skin runtime snapshot versus contract X(p0); no smoothing or pose deformation. Extra core morph states recorded separately.',bind_comparison=bindings,poses=results,scope='Original VaM runtime with supplied appearance assets. Body physics disabled; recorded SetDAZMorph drivers off/on; shape MCM preserved. Facial automatic motion is recorded but not part of lower-body acceptance. Extra deformation plugins excluded. Units cm. No UE runtime equivalence claim where actual transforms or body identity differ.')
    trace=read(ROOT/'formula-trace.json'); rb={b['id']:b for b in standing['bones']}; bindrows={b['bone']:b for b in bindings}; repairs=[]
    for f in trace:
        b=rb.get(f['target']); kind=f['target_type']; f['repair_status']='candidate_only'
        if b and kind.startswith(('Orientation','Rotation')):
            field='morphOrientationOffsets' if kind.startswith('Orientation') else 'morphRotations'
            matches={k:v for k,v in b[field].items() if k.endswith(':'+str(f['morph'])) or k==f['morph']}
            axis='XYZ'.index(kind[-1]); contribution=sum(float(v[axis]) for v in matches.values())
            f['runtime_contribution_degrees']=contribution;f['runtime_formula_keys']=list(matches)
            f['runtime_rotation_morphs_enabled']=b['rotationMorphsEnabled'];f['runtime_joint_rotation_disabled']=b['jointRotationDisabled']
            f['evidence']='runtime/core-off-standing.json bones[id].'+field
            if kind.startswith('Orientation') and abs(contribution)>1e-6 and bindrows.get(f['target'],{}).get('orientation_error_deg',0)>0.01:
                f['repair_status']='proven_runtime_bind_orientation_omission';repairs.append({k:f[k] for k in ['source_id','morph','target','target_type','runtime_contribution_degrees','evidence']})
    (ROOT/'formula-trace.json').write_text(json.dumps(trace,ensure_ascii=False,indent=2),encoding='utf-8')
    audit=read(ROOT/'contract-audit.json');audit['runtime_status']='12 lower-body pose pairs captured; acceptance is explicit per pose';audit['proven_repair_list']=repairs
    audit['remaining_formula_scope']='Rotation and scale formulas are not promoted merely because they are present. No per-formula ablation performed.'
    (ROOT/'contract-audit.json').write_text(json.dumps(audit,ensure_ascii=False,indent=2),encoding='utf-8')
    (ROOT/'runtime-audit.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(identity_matches=sum(x['matched'] for x in identity),identity_total=len(identity),shape=shape_metric,pose_count=len(results),paired=sum(x['core_on_captured'] for x in results)),ensure_ascii=False))


def report_c15(root):
    """Reuse C1 capture and A1/B2 ancestry, with explicit regional acceptance."""
    import csv
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.collections import LineCollection,PolyCollection
    root=Path(root);hist=Path(__file__).resolve().parents[1]/'Saved/LowerBodyContact';c=read(root/'contract.json');runtime=root/'source-repeat/runtime'
    shape=np.load(root/'shape-repair.npz');source_ids=np.asarray(c['body']['converted_to_source_vertex']);n=len(source_ids)
    detail=read(hist/'B2/fields/detail.json');rois={}
    # The original input prefix is preserved through deterministic refinement.
    oldbundle=Path(read(hist/'B2/poses/suite.json')['states'][0]['audit_bundle']);oldmap=read(oldbundle/'mapping.json')
    assert np.array_equal(oldmap['input_to_source'][:n],source_ids)
    for side in range(2):
        for region in ['anterior_hip','lateral_glute','glute_thigh']:
            name=f's{side}_roi_{region}';field=detail['fields'][name];rois[f'{side}_{region}']=np.fromfile(hist/'B2/fields'/field['file'],dtype='<f4')[:n]
    bones={b['name']:b for b in c['bones']};knee_specs=[]
    for side,prefix,sign in [(0,'l',1),(1,'r',-1)]:
        knee=np.asarray(bones[prefix+'Shin']['world_bind'])[:3,3];sign=float(np.sign(knee[1]));d=shape['shape']-knee;vertical=np.clip(1-np.abs(d[:,2])/7,0,1);side_mask=(shape['shape'][:,1]*sign>0)
        for label,axis,direction in [('anterior',0,1),('posterior',0,-1),('medial',1,-sign),('lateral',1,sign)]:
            w=vertical*side_mask*np.clip(.5+direction*d[:,axis]/4,0,1);rois[f'{side}_knee_{label}']=w
        knee_specs.append({'side':side,'anchor_bone':prefix+'Shin','center_ue_cm':knee.tolist(),'z_halfwidth_cm':7,'direction_fade_cm':4,'frame':'source appearance UE component frame; engineering ROI, not tissue segmentation'})
    np.savez_compressed(root/'c2-region-masks.npz',source_ids=source_ids,**rois)
    (root/'c2-region-contract.json').write_text(json.dumps({'contract_id':c['contract_id'],'glute_regions':'Exact B2 input-domain masks; old input prefix verified by source IDs','knees':knee_specs,'threshold':.05,'rule':'Deduplicate source aliases; no nearest-neighbour correspondence'},indent=2),encoding='utf8')
    _,first=np.unique(source_ids,return_index=True);unique=np.zeros(n,bool);unique[first]=True
    tri=np.concatenate([np.asarray(x,dtype=int).reshape(-1,3) for x in c['body']['sections']]);used=np.zeros(n,bool);used[np.unique(tri)]=True;unique &=used
    def stats(a,b,w):
        ids=np.flatnonzero((w>.05)&unique);err=np.linalg.norm(a[ids]-b[ids],axis=1)
        if not len(ids):return {'count':0}
        return {'count':len(ids),'rms_cm':float(np.sqrt(np.mean(err**2))),'p95_cm':float(np.percentile(err,95)),'maximum_cm':float(err.max()),'worst_source_vertex':int(source_ids[ids[err.argmax()]])}
    def normals(v):
        out=np.zeros_like(v);faces=np.cross(v[tri[:,1]]-v[tri[:,0]],v[tri[:,2]]-v[tri[:,0]])
        for k in range(3):np.add.at(out,tri[:,k],faces)
        length=np.linalg.norm(out,axis=1);return out/np.maximum(length[:,None],1e-15),length>1e-10
    def rows_for(label,a,b,pose):
        na,va=normals(a);nb,vb=normals(b);angle=np.degrees(np.arccos(np.clip(np.sum(na*nb,axis=1),-1,1)));out=[]
        for name,w in rois.items():
            row={'pose':pose,'comparison':label,'region':name,**stats(a,b,w)};ids=(w>.05)&unique&va&vb
            row.update(normal_p95_degrees=float(np.percentile(angle[ids],95)) if ids.any() else None,normal_max_degrees=float(angle[ids].max()) if ids.any() else None)
            out.append(row)
        return out
    static=rows_for('static source input',shape['shape'],shape['actual'],'standing')
    for row in static:row['c2_static_input_eligible']=row.get('maximum_cm',1)<.005;row['gate_tolerance_cm']=.005
    (root/'regional-shape-gates.json').write_text(json.dumps(static,indent=2),encoding='utf8')
    def ue_surface(bundle,stage,reference):
        mapping=read(bundle/('source-mapping.json' if stage=='00-source-lbs' else 'mapping.json'));r2i=np.array(mapping['render_to_input']);i2s=np.array(mapping['input_to_source']);ids=i2s[r2i];v=np.array(read(bundle/stage/'surface.json')['positions']);valid=(ids>=0)&(ids<n);acc=np.zeros((n,3));count=np.zeros(n);np.add.at(acc,ids[valid],v[valid]);np.add.at(count,ids[valid],1);present=count>0;out=reference.copy();out[present]=acc[present]/count[present,None];out=out[source_ids]
        return out,present[source_ids],int(np.sum(ids<0))
    requests={x['request_id']:x for x in read(root/'matrix-requests.json')};records=read(root/'ue-replays.json');allrows=[];gates=[];sections=[];figures=[]
    for entry in [e for e in records if e['mode']=='off']:
        pose=entry['pose'];bundle=Path(entry['audit_bundle']);off=verts(runtime/f'core-off-{pose}-raw.f32')[:,[2,0,1]]*100;on=verts(runtime/f'core-on-{pose}-raw.f32')[:,[2,0,1]]*100
        # Stored raw surface filename is authoritative if buffer suffix differs.
        vals={};coverage=None
        for stage in ['00-source-lbs','01-final-weights','02-static-helpers','03-corrective']:
            vals[stage],present,refined=ue_surface(bundle,stage,off);coverage=present if coverage is None else coverage&present
        data=read(bundle/'00-source-lbs/inputs.json');request=requests[entry['request_id']];matrix_error=max(np.max(np.abs(np.array(b['component_matrix_row_major'])-np.array(request['bones'][b['name']]))) for b in data['bones'] if b['name'] in request['bones'])
        gates.append({'pose':pose,'matrix_max_abs':float(matrix_error),'actual_ue_asset':read(bundle/'manifest.json')['stages']['01-final-weights']['mesh'],'source_capture':entry['source_capture'],'mapped_original_vertex_count':int(np.sum(coverage&unique)),'refined_render_vertices_excluded_from_measured_source_metrics':refined,'matrix_pass':bool(matrix_error<1e-4),'scope':'Original source IDs only for measured error; refined vertices retain exact ancestry, are not independent VaM samples. Facial input differences excluded from C2 lower-body gates.'})
        pairs=[('VaM composite drivers',on,off),('source LBS vs VaM off',vals['00-source-lbs'],off),('final weights minus source LBS',vals['01-final-weights'],vals['00-source-lbs']),('static helpers minus final weights',vals['02-static-helpers'],vals['01-final-weights']),('G062 minus static helpers',vals['03-corrective'],vals['02-static-helpers']),('G062 final vs VaM on',vals['03-corrective'],on)]
        old_unique=unique.copy();unique &=coverage
        for label,a,b in pairs:allrows.extend(rows_for(label,a,b,pose))
        union=np.maximum.reduce(list(rois.values()));sel=union>.05
        fig,axes=plt.subplots(1,3,figsize=(15,6),layout='constrained')
        face_use=np.max(union[tri],axis=1)>.05;faces=tri[face_use]
        for ax,axes_ids,title in [(axes[0],[0,2],'Side: VaM / final UE'),(axes[1],[1,2],'Front: VaM / final UE')]:
            for v,color in [(on,'#228be6'),(vals['03-corrective'],'#e8590c')]:
                edges=np.concatenate([v[faces][:,[0,1]][:,:,axes_ids],v[faces][:,[1,2]][:,:,axes_ids],v[faces][:,[2,0]][:,:,axes_ids]])
                ax.add_collection(LineCollection(edges,colors=color,linewidths=.12,alpha=.38))
            ax.autoscale();ax.set_aspect('equal');ax.set_title(title);ax.set_xlabel('cm');ax.set_ylabel('cm')
        err=np.linalg.norm(vals['03-corrective']-on,axis=1);sc=axes[2].scatter(on[sel,0],on[sel,2],c=err[sel],s=2,cmap='magma',vmin=0);axes[2].set_aspect('equal');axes[2].set_title('Local final error (cm)');fig.colorbar(sc,ax=axes[2]);fig.suptitle(pose+' | actual source matrices; isolated geometry replay (not UE screenshot)');file=root/(pose+'-layers.png');fig.savefig(file,dpi=135);plt.close(fig);figures.append(file.name)
        for region,w in rois.items():
            ids=np.flatnonzero((w>.05)&unique)
            if not len(ids):continue
            center=float(np.median(on[ids,1]));f=tri[np.max(w[tri],axis=1)>.05];item={'pose':pose,'region':region,'plane':'UE Y = constant','plane_cm':center,'curves':{}}
            for label,v in [('vam_on',on),('source_lbs',vals['00-source-lbs']),('static_helpers',vals['02-static-helpers']),('g062',vals['03-corrective'])]:
                lines=[]
                for face in f:
                    points=[]
                    for i,j in [(0,1),(1,2),(2,0)]:
                        a,b=v[face[i]],v[face[j]];da,db=a[1]-center,b[1]-center
                        if da*db<0:points.append((a+(b-a)*da/(da-db))[[0,2]].tolist())
                    if len(points)==2:lines.append(points)
                item['curves'][label]=lines
            sections.append(item)
        unique=old_unique
    (root/'comparison-gates.json').write_text(json.dumps(gates,indent=2),encoding='utf8');(root/'local-cross-sections.json').write_text(json.dumps(sections,separators=(',',':')),encoding='utf8')
    with (root/'regions.csv').open('w',newline='',encoding='utf-8-sig') as f:
        w=csv.DictWriter(f,fieldnames=list(allrows[0]));w.writeheader();w.writerows(allrows)
    # A single morph is attributed only when every other coefficient and matrix is unchanged.
    base=read(runtime/'ablation-frozen-baseline.json');zero=read(runtime/'ablation-bend-zero.json');restored=read(runtime/'ablation-restored.json');bm={m['uid']:float(m['morphValue']) for m in base['morphs']};zm={m['uid']:float(m['morphValue']) for m in zero['morphs']};changed=[k for k in bm.keys()|zm.keys() if abs(bm.get(k,0)-zm.get(k,0))>1e-7];bone_delta=max(np.max(np.abs(np.array(a['worldMatrix'],float)-np.array(b['worldMatrix'],float))) for a,b in zip(base['bones'],zero['bones']));a=verts(runtime/base['raw']['file'])[:,[2,0,1]]*100;b=verts(runtime/zero['raw']['file'])[:,[2,0,1]]*100;z=verts(runtime/restored['raw']['file'])[:,[2,0,1]]*100
    ablation={'changed_morphs':changed,'bone_matrix_max_abs':float(bone_delta),'accepted':bool(changed==['Glute Bend Fix'] and bone_delta<1e-5),'maximum_surface_effect_cm':float(np.linalg.norm(a-b,axis=1).max()),'restore_max_cm':float(np.linalg.norm(a-z,axis=1).max()),'regions':rows_for('single Bend Fix',a,b,'seated'),'warning':'This is not the composite SetDAZMorph on/off difference.'}
    (root/'single-morph-ablation.json').write_text(json.dumps(ablation,indent=2),encoding='utf8')
    summary={'contract_id':c['contract_id'],'static_gates':static,'matrix_gates':gates,'regional_rows':len(allrows),'cross_sections':len(sections),'figures':figures,'single_ablation':{k:v for k,v in ablation.items() if k!='regions'},'safety_probes':'130-degree combinations remain engineering safety probes, not measured VaM targets.'};(root/'summary.json').write_text(json.dumps(summary,indent=2),encoding='utf8');print(json.dumps({'poses':len(gates),'rows':len(allrows),'ablation':summary['single_ablation']},indent=2))

def report_c15_contours(root):
    from PIL import Image,ImageDraw
    from scipy.ndimage import binary_erosion,distance_transform_edt
    root=Path(root);c=read(root/'contract.json');sid=np.array(c['body']['converted_to_source_vertex']);n=len(sid);rois=np.load(root/'c2-region-masks.npz');names=[k for k in rois.files if k!='source_ids'];union=np.maximum.reduce([rois[k] for k in names]);tri=np.concatenate([np.array(t).reshape(-1,3) for t in c['body']['sections']]);runtime=root/'source-repeat/runtime';rows=[]
    for entry in read(root/'ue-replays.json'):
        if entry['mode']!='off':continue
        pose=entry['pose'];bundle=Path(entry['audit_bundle']);mapping=read(bundle/'mapping.json');ids=np.array(mapping['input_to_source'])[np.array(mapping['render_to_input'])];surface=np.array(read(bundle/'03-corrective/surface.json')['positions']);target=verts(runtime/f'core-on-{pose}-raw.f32')[:,[2,0,1]]*100;out=np.zeros_like(target);count=np.zeros(n);valid=(ids>=0)&(ids<n);np.add.at(out,ids[valid],surface[valid]);np.add.at(count,ids[valid],1);out[count>0]/=count[count>0,None];out[count==0]=target[count==0];out=out[sid]
        cards=[]
        for view in ['side','front','oblique']:
            def project(v):return np.c_[v[:,0] if view=='side' else v[:,1] if view=='front' else (v[:,0]+v[:,1])*.70710678,v[:,2]]
            a,b=project(target),project(out);bounds=np.concatenate([a[union>.05],b[union>.05]]);lo=bounds.min(0)-3;hi=bounds.max(0)+3;scale=float(max(hi-lo)/720);width=768;height=768
            def px(v):return np.c_[(v[:,0]-lo[0])/scale+20,height-20-(v[:,1]-lo[1])/scale]
            aa,bb=px(a),px(b)
            def raster(xy):
                im=Image.new('1',(width,height));draw=ImageDraw.Draw(im)
                for face in tri:draw.polygon([tuple(x) for x in xy[face]],fill=1)
                return np.array(im,dtype=bool)
            ma,mb=raster(aa),raster(bb);ea=ma&~binary_erosion(ma);eb=mb&~binary_erosion(mb);da=distance_transform_edt(~ea);db=distance_transform_edt(~eb);rgb=np.full((height,width,3),245,dtype=np.uint8);rgb[ma|mb]=[221,225,231];rgb[ea]=[30,115,225];rgb[eb]=[230,90,20];im=Image.fromarray(rgb);ImageDraw.Draw(im).text((12,10),pose+' / '+view+' | blue VaM, orange UE',fill='black');im.save(root/(pose+'-'+view+'-contour.png'))
            for name in names:
                points=np.concatenate([aa[rois[name]>.05],bb[rois[name]>.05]]);p0=np.floor(points.min(0)-5).astype(int);p1=np.ceil(points.max(0)+5).astype(int);mask=np.zeros_like(ea);mask[max(0,p0[1]):min(height,p1[1]+1),max(0,p0[0]):min(width,p1[0]+1)]=True;x=ea&mask;y=eb&mask;row={'pose':pose,'view':view,'region':name,'pixel_size_cm':scale,'visible':bool(x.any() and y.any()),'scope':'Visible full-body silhouette inside projected ROI bounds; occluded regions remain unavailable.'}
                if row['visible']:
                    e=np.r_[db[x],da[y]]*scale;row.update(p95_cm=float(np.percentile(e,95)),maximum_cm=float(e.max()),mean_cm=float(e.mean()))
                rows.append(row)
    (root/'regional-contours.json').write_text(json.dumps(rows,indent=2),encoding='utf8')

if __name__=='__main__':
    import argparse
    ap=argparse.ArgumentParser();ap.add_argument('--c15-root',type=Path);ap.add_argument('--contours-only',action='store_true');args=ap.parse_args()
    if args.c15_root:
        if args.contours_only:report_c15_contours(args.c15_root)
        else:report_c15(args.c15_root)
    else:
        if (ROOT.parent/'C1.5/latest-run.txt').exists():raise SystemExit('Historical C1 is frozen; use an independent C1.5 output')
        main()
