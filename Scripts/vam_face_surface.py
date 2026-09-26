"""Editor-only smooth closest-surface transfer onto an existing MH topology.

No topology replacement, person-specific coordinates, or runtime deformation.
Sparse linear solve regularizes displacement; inverted triangles reject output.
"""
import numpy as np
from scipy.spatial import cKDTree
from scipy import sparse
from scipy.sparse.linalg import cg, LinearOperator
from vam_face_calibration import Camera, CalibrationError

def normals(v,t):
    n=np.zeros_like(v);fn=np.cross(v[t[:,2]]-v[t[:,0]],v[t[:,1]]-v[t[:,0]])
    for j in range(3):np.add.at(n,t[:,j],fn)
    n/=np.maximum(np.linalg.norm(n,axis=1)[:,None],1e-12)
    fn/=np.maximum(np.linalg.norm(fn,axis=1)[:,None],1e-12)
    return n,fn

def closest(points,triangles):
    """One point per triangle, closest point on triangle including its edges."""
    a,b,c=triangles[:,0],triangles[:,1],triangles[:,2]
    ab=b-a;ac=c-a;n=np.cross(ab,ac);nn=np.sum(n*n,axis=1)
    q=points-n*np.divide(np.sum((points-a)*n,axis=1),nn,out=np.zeros_like(nn),where=nn>1e-16)[:,None]
    aq=q-a;d00=np.sum(ab*ab,1);d01=np.sum(ab*ac,1);d11=np.sum(ac*ac,1)
    d20=np.sum(aq*ab,1);d21=np.sum(aq*ac,1);den=d00*d11-d01*d01
    u=np.divide(d11*d20-d01*d21,den,out=np.full_like(den,-1),where=abs(den)>1e-16)
    v=np.divide(d00*d21-d01*d20,den,out=np.full_like(den,-1),where=abs(den)>1e-16)
    inside=(u>=0)&(v>=0)&(u+v<=1)&(nn>1e-16)
    candidates=[]
    for start,end in ((a,b),(b,c),(c,a)):
        edge=end-start;length=np.sum(edge*edge,1)
        fraction=np.clip(np.divide(np.sum((points-start)*edge,1),length,out=np.zeros_like(length),where=length>1e-16),0,1)
        candidates.append(start+fraction[:,None]*edge)
    candidates=np.stack(candidates,axis=1);distance=np.sum((candidates-points[:,None,:])**2,axis=2)
    out=candidates[np.arange(len(points)),np.argmin(distance,axis=1)];out[inside]=q[inside]
    return out

def transfer(source,source_triangles,head,head_triangles,calibration,iterations=5):
    source=np.asarray(source,float);st=np.asarray(source_triangles,int).reshape(-1,3)
    base=np.asarray(head,float);t=np.asarray(head_triangles,int).reshape(-1,3);current=base.copy()
    camera=Camera(calibration);px,depth=camera.project(base)
    curves=calibration['CurveTrackingPoints']
    eyes=np.array([[p['X'],p['Y']] for n,c in curves.items() if 'eyelid' in n for p in c['TrackingPoints']])
    lips=np.array([[p['X'],p['Y']] for n,c in curves.items() if 'lip_upper_outer' in n for p in c['TrackingPoints']])
    if len(eyes)==0 or len(lips)==0:raise CalibrationError('MissingFaceFrame')
    width=float(np.ptp(eyes[:,0]));center=float((eyes[:,0].min()+eyes[:,0].max())/2);eye=float(np.median(eyes[:,1]));gap=float(lips[:,1].min()-eye)
    if width<=0 or gap<=0:raise CalibrationError('InvalidFaceFrame')
    x=(px[:,0]-center)/width;y=(px[:,1]-eye)/gap
    candidate=(abs(x)<.6)&(y>-.45)&(y<1.65)
    ref=float(np.median(depth[candidate]));world_width=width*ref/camera.focal
    candidate &= depth<ref+.25*world_width
    # Open eyes/mouth and inner nostril surfaces have no reliable matching
    # interior in a skin-only input. Preserve aperture regions, rather than
    # collapsing their rims onto an unrelated nearest surface.
    for name,curve in curves.items():
        if 'eyelid' not in name and 'lip_' not in name:continue
        border=np.array([[q['X'],q['Y']] for q in curve['TrackingPoints']])
        margin=.012*width
        candidate &= ~((px[:,0]>border[:,0].min()-margin)&(px[:,0]<border[:,0].max()+margin)&
                       (px[:,1]>border[:,1].min()-margin)&(px[:,1]<border[:,1].max()+margin))
    blend=np.minimum.reduce([(.6-abs(x))/.1,(y+.45)/.25,(1.65-y)/.25,np.ones(len(base))]);blend=np.clip(blend,0,1)*candidate
    ids=np.flatnonzero(blend>0)
    if len(ids)<30:raise CalibrationError('InsufficientFacePatch')
    # Only candidate triangles near the face in camera depth can receive hits.
    sp,sd=camera.project(source);sx=(sp[:,0]-center)/width;sy=(sp[:,1]-eye)/gap
    usable=(abs(sx)<.8)&(sy>-.8)&(sy<2.)&(sd<ref+.3*world_width)
    st=st[np.any(usable[st],axis=1)]
    _,sn=normals(source,st);triangles=source[st];tree=cKDTree(triangles.mean(axis=1))
    edges=np.concatenate([t[:,[0,1]],t[:,[1,2]],t[:,[2,0]]]);edges=np.concatenate([edges,edges[:,::-1]])
    adjacency=sparse.coo_matrix((np.ones(len(edges)),(edges[:,0],edges[:,1])),shape=(len(base),len(base))).tocsr()
    adjacency.data[:]=1;degree=np.asarray(adjacency.sum(axis=1)).ravel()
    lap=sparse.eye(len(base))-sparse.diags(1/np.maximum(degree,1))@adjacency
    regularizer=10.0*(lap.T@lap);fixed=(1-blend)*1000
    old_n=np.cross(base[t[:,1]]-base[t[:,0]],base[t[:,2]]-base[t[:,0]])
    valid_triangles=np.linalg.norm(old_n,axis=1)>1e-10
    history=[]
    for iteration in range(iterations):
        vn,_=normals(current,t);query=current[ids];k=min(32,len(triangles))
        _,index=tree.query(query,k=k);index=np.asarray(index).reshape(len(query),k)
        hits=closest(np.repeat(query,k,axis=0),triangles[index.ravel()]).reshape(len(query),k,3)
        distances=np.sum((hits-query[:,None,:])**2,axis=2)
        compatible=np.sum(sn[index]*vn[ids,None,:],axis=2)>.6
        distances=np.where(compatible,distances,np.inf);pick=np.argmin(distances,axis=1)
        error=np.sqrt(distances[np.arange(len(query)),pick]);valid=error<.04*world_width
        matched=ids[valid];target=hits[np.arange(len(query)),pick][valid]
        weights=np.zeros(len(base));weights[matched]=blend[matched]
        matrix=(regularizer+sparse.diags(weights+fixed+.001)).tocsr()
        inverse=1/matrix.diagonal();preconditioner=LinearOperator(matrix.shape,matvec=lambda v:inverse*v)
        rhs=np.zeros_like(base);rhs[matched]=weights[matched,None]*(target-base[matched])
        delta=np.empty_like(base)
        for axis in range(3):
            delta[:,axis],status=cg(matrix,rhs[:,axis],M=preconditioner,rtol=1e-7,maxiter=600)
            if status!=0:raise CalibrationError('SurfaceLinearSolveIncomplete')
        proposed=base+delta;proposed[blend==0]=base[blend==0]
        alpha=1.
        while alpha>=1/256:
            accepted=current+alpha*(proposed-current)
            trial_n=np.cross(accepted[t[:,1]]-accepted[t[:,0]],accepted[t[:,2]]-accepted[t[:,0]])
            if np.all(np.sum(old_n*trial_n,axis=1)[valid_triangles]>0):break
            alpha*=.5
        if alpha<1/256:break
        current=accepted
        history.append({'iteration':iteration,'matched':len(matched),'step_scale':alpha,'pre_solve_mean_distance_cm':float(error[valid].mean())})
    new_n=np.cross(current[t[:,1]]-current[t[:,0]],current[t[:,2]]-current[t[:,0]])
    flipped=(np.sum(old_n*new_n,axis=1)<=0)&(np.linalg.norm(old_n,axis=1)>1e-10)
    # Do not silently accept self-folds even if point-to-surface error decreases.
    report={'iterations':history,'flipped_triangles':int(flipped.sum()),'max_displacement_cm':float(np.linalg.norm(current-base,axis=1).max()),'visual_acceptance_passed':False}
    if flipped.any():raise CalibrationError('SurfaceTriangleInversion:'+str(report))
    return current,report
