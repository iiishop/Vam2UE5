"""Constrained LBS approximation of DAZSkinV2 single-joint rotation responses.

Reference is an explicit port of the CPU axis rotation/bulge equations, NOT
captured VaM runtime ground truth. Compound poses, scale and joint corrections
remain outside this calibration. Raw input is retained by the caller.
"""
import math
import numpy as np
from vam_decode import require


def simplex(v):
    u=np.sort(v)[::-1]; sums=np.cumsum(u)-1
    rho=np.flatnonzero(u-sums/np.arange(1,len(v)+1)>0)[-1]
    return np.maximum(v-sums[rho]/(rho+1),0)


def axis_response(local,axis,angle,weight,bulge_left,bulge_right,factors,bulge_scale):
    # Local axes are cyclically converted into UE (z,x,y), a proper rotation.
    a,b=(axis+1)%3,(axis+2)%3
    p=local.copy();theta=angle*weight;c=np.cos(theta);s=np.sin(theta)
    p[:,a]=local[:,a]*c-local[:,b]*s
    p[:,b]=local[:,a]*s+local[:,b]*c
    if abs(angle)>.01:
        sign='pos' if angle>0 else 'neg'
        for side,w in (('left',bulge_left),('right',bulge_right)):
            f=1+factors.get(sign+side,0)*angle*bulge_scale*w
            p[:,a]*=f;p[:,b]*=f
    return p-local


def fit(raw,vertices,bones,max_influences=8):
    vertices=np.asarray(vertices,dtype=np.float64);n=len(vertices);b=len(bones)
    lookup={bone['name']:i for i,bone in enumerate(bones)}
    descendants=np.eye(b,dtype=float)
    for i,bone in enumerate(bones):
        parent=bone['parent'];seen={i}
        while parent>=0:
            require(parent not in seen,'bone_cycle',bone['name']);seen.add(parent)
            descendants[parent,i]=1;parent=bones[parent]['parent']
    h=np.zeros((n,b));g=np.zeros((n,b));observations=[]
    support=np.zeros((n,b),dtype=bool)
    scale=float(raw.get('bulgeScale',1))
    for node in raw['nodes']:
        bi=lookup[node['name']];weights=np.zeros((n,3));left=np.zeros((n,3));right=np.zeros((n,3))
        # Source rows have distinct vertices. Reject ambiguity rather than blend duplicates.
        ids=[r['vertex'] for r in node['weights']]
        require(len(ids)==len(set(ids)),'triax_duplicate',node['name'])
        for row in node['weights']:
            v=row['vertex'];require(0<=v<n,'skin_domain',str(v))
            for source_axis,axis in enumerate((1,2,0)):
                key='xyz'[source_axis];weights[v,axis]=row[key+'weight']
                left[v,axis]=row[key+'leftbulge'];right[v,axis]=row[key+'rightbulge']
        full=np.zeros(n,dtype=bool)
        for v in node['fullyWeightedVertices']:
            require(isinstance(v,int) and 0<=v<n,'skin_domain',str(v))
            weights[v,:]=1
            full[v]=True
        active=np.any(weights>0,axis=1)
        require(not np.any(support[:,bi]&active),'triax_domain_overlap',node['name'])
        support[:,bi]|=active
        bind=np.asarray(bones[bi]['world_bind']);basis=bind[:3,:3];center=bind[:3,3]
        local=(vertices-center)@basis
        for source_axis,axis in enumerate((1,2,0)):
            factors={sign+side:node['bulgeFactors'].get('xyz'[source_axis]+sign+side,0) for sign in ('pos','neg') for side in ('left','right')}
            for degrees in (-30,-15,15,30):
                angle=math.radians(degrees)
                rigid=axis_response(local,axis,angle,np.ones(n),np.zeros(n),np.zeros(n),{},0)
                target=axis_response(local,axis,angle,weights[:,axis],left[:,axis],right[:,axis],factors,scale)
                target[full]=rigid[full]
                # Repeated merged body/graft node names occupy disjoint source domains.
                # Each domain contributes only its own response observations.
                h[active,bi]+=np.sum(rigid[active]*rigid[active],axis=1)
                g[active,bi]+=np.sum(rigid[active]*target[active],axis=1)
            # Distinct angles are held out of optimization; errors are port-reference errors.
            observations.append((bi,axis,local,weights[:,axis],left[:,axis],right[:,axis],factors,full,active))
    result=np.zeros((n,b));uncovered=[]
    for v in range(n):
        if v%100==0:
            from vam_native_job_state import check_cancel
            check_cancel()
        candidates=np.flatnonzero(support[v])
        if not len(candidates):uncovered.append(v);continue
        def solve(candidates):
            d=descendants[:,candidates]
            H=d.T@(h[v,:,None]*d);G=d.T@g[v]
            L=max(float(np.linalg.norm(H,ord=2)),1e-12)
            x=np.full(len(candidates),1/len(candidates));y=x.copy();t=1.
            for _ in range(100):
                z=simplex(y-(H@y-G)/L);nt=(1+math.sqrt(1+4*t*t))/2
                y=z+(t-1)/nt*(z-x);x=z;t=nt
            return x
        values=solve(candidates)
        if len(candidates)>max_influences:
            keep=np.argsort(-values,kind='stable')[:max_influences];candidates=candidates[keep];values=solve(candidates)
        result[v,candidates]=values
    require(not uncovered,'triax_uncovered',str(uncovered[:16]))
    require(np.all(np.isfinite(result)) and np.max(np.abs(result.sum(axis=1)-1))<1e-8,'fit_weights','Invalid constrained fit')
    cumulative=result@descendants.T;errors=[];by_joint={}
    for bi,axis,local,w,left,right,factors,full,active in observations:
        for degrees in (-22,22):
            angle=math.radians(degrees)
            rigid=axis_response(local,axis,angle,np.ones(n),np.zeros(n),np.zeros(n),{},0)
            target=axis_response(local,axis,angle,w,left,right,factors,scale);target[full]=rigid[full]
            sample=np.linalg.norm(rigid*cumulative[:,bi,None]-target,axis=1)[active].tolist()
            errors.extend(sample);by_joint.setdefault(bones[bi]['name'],[]).extend(sample)
    error=np.asarray(errors)
    rows=[[int(v),int(i),float(result[v,i])] for v,i in zip(*np.nonzero(result>1e-10))]
    return rows,{'method':'nonnegative simplex least squares on source CPU single-joint axis/bulge response',
        'reference':'DAZSkinV2.SkinMeshPart mathematical port; not captured runtime ground truth',
        'training_angles_degrees':[-30,-15,15,30],'validation_angles_degrees':[-22,22],
        'validation_scope':'isolated rotations; no compound-pose, scale, smoothing or physics validation',
        'max_influences':max_influences,'rms_cm':float(np.sqrt(np.mean(error**2))),
        'p95_cm':float(np.percentile(error,95)),'maximum_cm':float(np.max(error)),
        'per_joint':{name:{'rms_cm':float(np.sqrt(np.mean(np.asarray(e)**2))),'p95_cm':float(np.percentile(e,95)),'maximum_cm':float(np.max(e))} for name,e in by_joint.items() if e},
        'status':'pending_runtime_calibration'}
