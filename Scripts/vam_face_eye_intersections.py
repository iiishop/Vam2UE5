"""Read-only neutral geometry diagnostics; crossings are not anatomical verdicts.

Proper transverse intersections only: coplanar/tangent contacts are not covered.
Auxiliary eye surfaces may intentionally overlap; never delete or offset them
based solely on this diagnostic.
"""
import argparse,json
from pathlib import Path
import numpy as np
from scipy.spatial import cKDTree
from vam_face_surface import closest


def edge_crosses(a,b,tri):
    direction=b-a;e1=tri[:,1]-tri[:,0];e2=tri[:,2]-tri[:,0]
    h=np.cross(direction,e2);det=np.sum(e1*h,axis=1)
    scale=np.linalg.norm(direction,axis=1)*np.linalg.norm(e1,axis=1)*np.linalg.norm(e2,axis=1)
    valid=np.abs(det)>1e-10*np.maximum(scale,1e-30)
    inv=np.divide(1.,det,out=np.zeros_like(det),where=valid)
    s=a-tri[:,0];u=inv*np.sum(s*h,axis=1);q=np.cross(s,e1)
    v=inv*np.sum(direction*q,axis=1);t=inv*np.sum(e2*q,axis=1)
    eps=1e-7
    return valid&(u>eps)&(v>eps)&(u+v<1-eps)&(t>eps)&(t<1-eps)


def candidate_pairs(vertices,first,second,same_mesh=False,proposed=None,padding=0.):
    a=vertices[first];b=vertices[second]
    if proposed is not None:
        a=np.concatenate([a,proposed[first]],axis=1);b=np.concatenate([b,proposed[second]],axis=1)
    ca=a.mean(1);cb=b.mean(1)
    ra=np.linalg.norm(a-ca[:,None],axis=2).max(1);rb=np.linalg.norm(b-cb[:,None],axis=2).max(1)
    tree=cKDTree(cb);pairs=[]
    for i,candidates in enumerate(tree.query_ball_point(ca,ra+rb.max()+padding)):
        j=np.asarray(candidates,int)
        if not len(j):continue
        keep=np.all(a[i].max(0)+padding>b[j].min(1),axis=1)&np.all(b[j].max(1)+padding>a[i].min(0),axis=1)
        if same_mesh:keep&=~np.any(first[i][None,:,None]==second[j][:,None,:],axis=(1,2))
        j=j[keep]
        if len(j):pairs.extend((i,int(k)) for k in j)
    return np.asarray(pairs,int).reshape(-1,2)


def crossing_mask(vertices,first,second,pairs):
    x=vertices[first[pairs[:,0]]];y=vertices[second[pairs[:,1]]];hit=np.zeros(len(pairs),bool)
    for k in range(3):
        hit|=edge_crosses(x[:,k],x[:,(k+1)%3],y)
        hit|=edge_crosses(y[:,k],y[:,(k+1)%3],x)
    return hit


def crossings(vertices,first,second,same_mesh=False):
    pairs=candidate_pairs(vertices,first,second,same_mesh)
    return pairs[crossing_mask(vertices,first,second,pairs)].tolist()


def pair_distances(vertices,first,second,pairs):
    """Triangle distance: vertex/face, edge/edge, then transverse crossings."""
    x=vertices[first[pairs[:,0]]];y=vertices[second[pairs[:,1]]]
    distance=np.full(len(pairs),np.inf)
    def dot(a,b):return np.sum(a*b,axis=1)
    for k in range(3):
        distance=np.minimum(distance,np.linalg.norm(x[:,k]-closest(x[:,k],y),axis=1))
        distance=np.minimum(distance,np.linalg.norm(y[:,k]-closest(y[:,k],x),axis=1))
        for j in range(3):
            p=x[:,k];q=y[:,j];u=x[:,(k+1)%3]-p;v=y[:,(j+1)%3]-q;w=p-q
            a=dot(u,u);b=dot(u,v);c=dot(v,v);d=dot(u,w);e=dot(v,w);den=a*c-b*b
            valid=den>1e-12*np.maximum(a*c,1e-30)
            s=np.divide(b*e-c*d,den,out=np.zeros_like(den),where=valid)
            t=np.divide(a*e-b*d,den,out=np.zeros_like(den),where=valid)
            interior=valid&(s>=0)&(s<=1)&(t>=0)&(t<=1)
            distance=np.minimum(distance,np.where(interior,np.linalg.norm(w+s[:,None]*u-t[:,None]*v,axis=1),np.inf))
    # Endpoint/edge minima are included in vertex/triangle distances above.
    distance[crossing_mask(vertices,first,second,pairs)]=0.
    return distance


def report(export,landmarks,output):
    export=Path(export)
    read=lambda p:json.loads(Path(p).read_text(encoding='utf8'))
    face=read(export/'actual-face.json');sections=read(export/'actual-sections.json')['sections']
    lm=read(landmarks);v=np.asarray(face['vertices'],float)
    skin=np.asarray(next(s['triangles'] for s in sections if s['slot']=='head_shader_shader')).reshape(-1,3)
    seeds=set(sum([lm['crv_eyelid_'+part+'_'+side]['vIDs'] for side in ('l','r') for part in ('upper','lower')],[]))
    roi=set(seeds)
    for _ in range(3):roi.update(skin[np.any(np.isin(skin,list(roi)),axis=1)].ravel().tolist())
    tri_ids=np.flatnonzero(np.any(np.isin(skin,list(roi)),axis=1));eye=skin[tri_ids]
    results={}
    for section in sections:
        slot=section['slot']
        if slot in ('teeth_shader_shader','saliva_shader_shader'):continue
        other=np.asarray(section['triangles']).reshape(-1,3)
        pairs=crossings(v,eye,other,slot=='head_shader_shader')
        results[slot]={'proper_crossing_pairs':len(pairs),
                      'pairs_skin_triangle_section_triangle':[[int(tri_ids[a]),b] for a,b in pairs]}
    result={'scope':'neutral eye skin 3-ring ROI vs exported sections; proper transverse triangle intersections only',
            'not_covered':['coplanar overlap','tangency','animation','rendered depth/material artifacts'],
            'anatomical_or_visual_pass':False,'eye_roi_triangles':len(eye),'sections':results}
    Path(output).write_text(json.dumps(result,indent=2),encoding='utf8')
    return {key:value['proper_crossing_pairs'] for key,value in results.items()}


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--export',required=True);p.add_argument('--landmarks',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();print(json.dumps(report(a.export,a.landmarks,a.output)))
