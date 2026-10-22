"""Bake source point support and authored links onto imported UE guide nodes.

No hairstyle names or spatial hair-to-hair collision inference are used. Native
Groom has a virtual node followed by 31 physical nodes. Link distances are rebuilt
in the measured, body-fitted rest shape, not copied between metres and centimetres.
"""
import argparse, hashlib, json
from pathlib import Path
import numpy as np


def collision_radii(group, hair, guide_ids):
    """Transfer authored particle radii through the same visible-guide provenance.

    VaM BuildParticles uses radius 0 at root, StandRootRadius at point 1,
    StandRadius afterwards. WorldScale is the captured source scale; metres
    become UE centimetres. This does not infer radius from render hair width.
    """
    native=group['native_physics']
    scale=float(native['WorldScale'])
    body=float(native['StandRadius'])*scale*100
    root=float(native['StandRootRadius'])*scale*100 if str(native['UseSeparateRootRadius']).lower()=='true' else body
    if not np.isfinite([scale,body,root]).all() or scale<=0 or min(body,root)<0:raise ValueError('Invalid source collision radii')
    n=int(hair['segments']);profile=np.full(n,body);profile[0]=0;profile[1]=root
    curves={c['source_curve']:c for c in group['curves']}
    source=np.asarray(hair['vertices']).reshape(-1,n,3)
    u=np.maximum(0,np.arange(32)-1)/30
    result=[]
    for sid in guide_ids:
        c=curves[sid];binding=c.get('visible_binding')
        if binding:
            result.append(np.interp(u*binding['length'],np.linspace(0,1,n),profile).tolist())
        else:
            arc=np.r_[0,np.linalg.norm(np.diff(source[sid],axis=0),axis=1).cumsum()]
            if arc[-1]<=0:raise ValueError('Degenerate source radius guide')
            result.append(np.interp(u,arc/arc[-1],profile).tolist())
    return dict(node_radii_cm=result,body_radius_cm=body,root_radius_cm=root,
                source_scale=scale,source_segments=n,
                method='VaM BuildParticles root/first/mobile radius, transferred by source coordinate; m to cm')


def fit_visible_guides(group, hair, budget=128):
    """UE simulates representative visible strands, not buried VaM control paths.

    Cover visible geometry first; transfer material using recorded source triangle
    and length provenance. No spatial guess is used for source support identity.
    """
    from vam_hair_prepare import resample
    visible=np.asarray([resample(p,32) for p in group['render_curves']],float)
    budget=min(budget,len(group['curves']))
    if not np.isfinite(visible).all():raise ValueError('Invalid visible hair geometry')
    sample=visible[:,[0,8,16,24,31]]
    selected=[];distance=np.full(len(visible),np.inf);nxt=0
    for _ in range(min(budget,len(visible))):
        selected.append(nxt)
        distance=np.minimum(distance,((sample-sample[nxt])**2).mean((1,2)))
        nxt=int(distance.argmax())
        if distance[nxt]<1e-8:break
    bindings=group['render_source_bindings']
    assert len(bindings)==len(visible)
    support=point_rigidity(hair,group['settings']);curves=[]
    # Detect support in the source control domain BEFORE length/spread expansion.
    # Generated visible roots can be closely clustered and hide that convergence.
    original=np.asarray([c['points'] for c in group['curves']],float)
    source_points=np.asarray(hair['vertices'],float).reshape(-1,int(hair['segments']),3)
    arcs=np.concatenate([np.zeros((len(source_points),1)),np.linalg.norm(np.diff(source_points,axis=1),axis=2).cumsum(1)],axis=1)
    arcs/=np.maximum(arcs[:,-1:],1e-10)
    original_support=np.asarray([np.interp(np.linspace(0,1,32),arcs[c['source_curve']],support[c['source_curve']]) for c in group['curves']])
    edges=sum(map(len,hair['nearby_groups'])) if float(group.get('native_physics',{}).get('NearbyJointPower',1))>0 else 0
    anchor=bundle_anchor(original,edges,original_support)
    if anchor:
        t=np.clip((arcs-anchor['curve_coordinate'])/.1,0,1)
        support=np.maximum(support,.995*(1-t*t*(3-2*t)))
        group['source_bundle_anchor']=anchor
    for index in selected:
        binding=bindings[index];u=np.linspace(0,1,32)*binding['length']
        values=sum(float(w)*np.interp(u,np.linspace(0,1,support.shape[1]),support[int(sid)])
                   for sid,w in zip(binding['source_curves'],binding['weights']))
        curves.append(dict(source_curve=index,points=visible[index].tolist(),rigidity_profile=values.tolist(),
                           rigidity=float(np.median(values)),anchor_index=0,visible_binding=binding))
    group['curves']=curves
    return dict(method='visible-coverage-source-triangle-support',render_curve_ids=selected,
                coverage_rms_cm=float(np.sqrt(3*distance.mean())),coverage_max_cm=float(np.sqrt(3*distance.max())),render_unchanged=True)


def ensure_render_bindings(group,hair,item,capture):
    if 'render_source_bindings' in group:return
    bary=np.fromfile(capture/(item['prefix']+'-barycentrics.f32'),dtype='<f4').reshape(-1,3)
    density=int(float(item['tess_factor'][0]));lengths=np.array([float(item['render_settings']['Length'+str(i)]) for i in (1,2,3)])
    group['render_source_bindings']=[dict(source_curves=list(map(int,tri)),weights=bary[int(tri[0])*64+min(63,int(line/density*64))].tolist(),
        length=float(bary[min(63,int(line/density*64))]@lengths))
        for tri in np.asarray(hair['indices']).reshape(-1,3) for line in range(density)]
    assert len(group['render_source_bindings'])==len(group['render_curves']), 'Render provenance mismatch'


def point_rigidity(hair, settings):
    segments = int(hair['segments'])
    count = len(hair['root_to_scalp'])
    if str(settings.get('usePaintedRigidity', 'true')).lower() == 'true' and hair['rigidities']:
        result = np.asarray(hair['rigidities'], float).reshape(count, segments).copy()
    else:
        u = np.maximum(0, np.arange(segments)-1)/max(1, segments-2)
        root, main, tip = (float(settings.get(k, default)) for k, default in
                           [('rootRigidity', .2), ('mainRigidity', .01), ('tipRigidity', 0)])
        values = tip+(main-tip)*(1-u)**float(settings.get('rigidityRolloffPower', 8))
        values[1] = root
        result = np.tile(values, (count, 1))
    result[:, 0] = 1
    return np.clip(result, 0, 1)


def bundle_anchor(curves, source_edges, rigidity):
    """Conservative, dimensionless convergence test, not a hairstyle name rule.

    Require authored cross-strand links, a neck less than half the root spread,
    a subsequent widening, and painted support at that neck. Loose sheets and
    tapering tips are not sufficient evidence for a tie.
    """
    if len(curves)<8 or not source_edges:return None
    center=np.median(curves,axis=0)
    radius=np.median(np.linalg.norm(curves-center,axis=2),axis=0)
    for k in range(4,17):
        if (radius[k]<=radius[k-1] and radius[k]<radius[k+1]
            and radius[k]<.5*radius[0] and max(radius[k+1:k+5])>1.15*radius[k]
            and np.median(rigidity[:,k])>.02):
            return dict(method='authored-links-and-supported-convergence',curve_coordinate=k/31,
                        root_spread_cm=float(radius[0]),neck_spread_cm=float(radius[k]),
                        index=k,center_cm=center[k].tolist())
    return None


def bake_group(group, hair, budget=128):
    curves = np.asarray([c['points'] for c in group['curves']], float)
    if curves.shape[1:] != (32, 3) or not np.isfinite(curves).all():
        raise ValueError('Expected finite 32-point measured guides')
    ids = [c['source_curve'] for c in group['curves']]
    by_id = {v: i for i, v in enumerate(ids)}
    # Same four-sample shape metric as the existing UE importer, deterministic.
    sample = curves[:, [0, 10, 20, 31]]
    selected, distance = [0], np.full(len(curves), np.inf)
    while len(selected) < min(budget, len(curves)):
        distance = np.minimum(distance, ((sample-sample[selected[-1]])**2).sum((1, 2)))
        nxt = int(np.argmax(distance))
        if distance[nxt] < 1e-8: break
        selected.append(nxt)
    owners = np.argmin(((sample[:, None]-sample[selected][None])**2).sum((2, 3)), axis=1)
    # Nodes 0/1 are the virtual and physical root respectively.
    u = np.maximum(0, np.arange(32)-1)/30
    source_rig = point_rigidity(hair, group['settings'])
    rig = np.array([np.interp(u, np.linspace(0, 1, 32), c['rigidity_profile'])
                    for c in group['curves']])[selected]
    # Reconstruct the source point-joint semantics even for older fit caches.
    # The source parameter is transferred through source arclength; it is never
    # replaced by a group-average bend stiffness.
    source_p = np.asarray(hair['vertices'], float).reshape(-1, int(hair['segments']), 3)
    for gi, ci in enumerate(selected):
        if 'visible_binding' in group['curves'][ci]:continue
        si = ids[ci]
        arc = np.r_[0, np.linalg.norm(np.diff(source_p[si], axis=0), axis=1).cumsum()]
        if arc[-1] <= 1e-10: raise ValueError('Degenerate source guide')
        rig[gi] = np.interp(u, arc/arc[-1], source_rig[si])
    native=group.get('native_physics',{})
    rig*=float(native.get('PaintedRigidityMultiplier',1))
    rig=np.clip(rig,0,1)
    raw_rig=rig.copy()
    power=float(native.get('NearbyJointPower',1))
    source_edges=sum(map(len,hair['nearby_groups']))
    anchor=bundle_anchor(curves[selected],source_edges if power>0 else 0,rig)
    if group.get('source_bundle_anchor'):
        anchor=dict(group['source_bundle_anchor'])
        lengths=[group['curves'][i]['visible_binding']['length'] for i in selected]
        anchor['curve_coordinate']=min(1,anchor['curve_coordinate']/max(float(np.median(lengths)),1e-8))
        anchor['method']='source-control-convergence-transferred-to-visible-guides'
    if anchor:
        t=np.clip((u-anchor['curve_coordinate'])/.1,0,1)
        support=.995*(1-t*t*(3-2*t))
        rig=np.maximum(rig,support)
    rest = np.array([[np.interp(u, np.linspace(0, 1, 32), curves[i, :, k])
                      for k in range(3)] for i in selected]).transpose(0, 2, 1)
    edges = {}
    segments = int(hair['segments'])
    source_owners={}
    for gi,ci in enumerate(selected):
        binding=group['curves'][ci].get('visible_binding')
        if binding:
            for sid,weight in zip(binding['source_curves'],binding['weights']):
                if weight>source_owners.get(sid,(-1,0,0))[1]:source_owners[sid]=(gi,weight,binding['length'])
    def node(source_vertex):
        sid, j = divmod(int(source_vertex), segments)
        if source_owners:
            if sid not in source_owners:return None
            owner,weight,length=source_owners[sid];u=j/max(1,segments-1)/max(length,1e-8)
            if u>1:return None
            return owner*32+int(np.clip(round(u*30)+1,1,31))
        if sid not in by_id: return None
        arc = np.r_[0, np.linalg.norm(np.diff(source_p[sid], axis=0), axis=1).cumsum()]
        return int(owners[by_id[sid]])*32+int(np.clip(round(arc[j]/max(arc[-1], 1e-10)*30)+1, 1, 31))
    for batch in hair['nearby_groups']:
        if power<=0:continue  # Retained edit data may be inactive in the source.
        for a, b, source_distance, _ in batch:
            na, nb = node(a), node(b)
            if na is None or nb is None or na//32 == nb//32: continue
            key = tuple(sorted((na, nb)))
            edges[key] = float(np.linalg.norm(rest.reshape(-1, 3)[na]-rest.reshape(-1, 3)[nb]))
    adjacency = [[] for _ in range(len(selected)*32)]
    for (a, b), length in edges.items():
        adjacency[a].append((b, length)); adjacency[b].append((a, length))
    # Bounded sparse, rest-distance links, not repulsive self collision. Retain
    # strongest/local links per node; receipt explicitly reports this reduction.
    links = []
    for neighbours in adjacency:
        chosen = sorted(neighbours, key=lambda x: (x[1], x[0]))[:2]
        links.append([list(x) for x in chosen]+[[-1, 0]]*(2-len(chosen)))
    result=dict(version='source-point-support-links-3', source_owns_shape=True, prediction_support=True,
                support_passes_per_second=60*max(1,int(float(native.get('Iterations',2)))),
                guide_source_ids=[ids[i] for i in selected],
                rigidity=np.clip(rig, 0, 1).tolist(), links=links,
                authored_rigidity=raw_rig.tolist(),automatic_bundle_anchor=anchor,
                nearby_rate_per_second=60*power,source_edges=source_edges, mapped_edges=len(edges),
                retained_directed_links=sum(x[0]>=0 for row in links for x in row),
                node_layout='virtual root + 31 physical nodes; imported guide order')
    if 'StandRadius' in native:
        result['source_collision']=collision_radii(group,hair,result['guide_source_ids'])
    return result


def main(source, output):
    data=json.loads(source.read_text())
    manifest=json.loads((Path(data['native_render_evaluation']['capture'])/'hair-rest.json').read_text())
    assets={a['source_path']:a['source']['dynamic']['hair'] for a in data['catalog']['assets']}
    for group in data['groups']:
        item=next(h for h in manifest['hairs'] if h['path'].split(':')[-1]==Path(group['source_path']).stem)
        group['native_physics']=item['physics_settings']
        ensure_render_bindings(group,assets[group['source_path']],item,Path(data['native_render_evaluation']['capture']))
        group['visible_guide_fit']=fit_visible_guides(group,assets[group['source_path']])
        group['source_constraints']=bake_group(group, assets[group['source_path']])
    data.pop('identity')
    data['identity']=hashlib.sha256(json.dumps(data,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
    output.parent.mkdir(parents=True,exist_ok=True)
    output.write_text(json.dumps(data,separators=(',',':')))
    print(json.dumps([dict(name=g['name'],guides=len(g['source_constraints']['guide_source_ids']),
                         links=g['source_constraints']['retained_directed_links']) for g in data['groups']]))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('source',type=Path);p.add_argument('output',type=Path)
    a=p.parse_args();main(a.source,a.output)
