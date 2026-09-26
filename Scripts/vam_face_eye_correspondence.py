"""Topology ordered, provisional eyelid correspondence (not semantic truth)."""
import numpy as np
from scipy.optimize import minimize, LinearConstraint


def ordered_cycle(ids, polygons):
    ids=set(map(int,ids)); adjacency={i:set() for i in ids}
    for polygon in polygons:
        for a,b in zip(polygon,polygon[1:]+polygon[:1]):
            if a in ids and b in ids:
                adjacency[a].add(b);adjacency[b].add(a)
    if not ids or any(len(v)!=2 for v in adjacency.values()):
        raise ValueError('EyelidIsNotSingleTopologyCycle')
    start=min(ids);cycle=[start];previous=None;current=start
    while True:
        nxt=min(adjacency[current]-({previous} if previous is not None else set()))
        if nxt==start:break
        if nxt in cycle:raise ValueError('EyelidCycleRepeatedVertex')
        cycle.append(nxt);previous,current=current,nxt
    if len(cycle)!=len(ids):raise ValueError('EyelidCycleDisconnected')
    return np.asarray(cycle,int)


def ordered_correspondence(target, source, source_normals=None):
    """Optimize one winding with bounded positive arc increments.

    Unlike independent nearest points this cannot reverse or collapse any
    interval. Both winding directions are tested without spatial axis rules.
    Source curve selection and canthus semantics remain unverified.
    """
    target=np.asarray(target,float);source=np.asarray(source,float)
    gaps=np.linalg.norm(np.roll(target,-1,axis=0)-target,axis=1)
    if min(gaps)<=1e-12:raise ValueError('DegenerateTargetEyelid')
    gaps/=sum(gaps);start=np.r_[0,np.cumsum(gaps)[:-1]]
    n=len(target);matrix=np.roll(np.eye(n),-1,axis=1)-np.eye(n)
    # row i is t[i+1]-t[i]; the closing increment additionally includes 1.
    matrix=np.zeros((n,n))
    for i in range(n):matrix[i,i]=-1;matrix[i,(i+1)%n]=1
    closing=np.zeros(n);closing[-1]=1
    constraint=LinearConstraint(matrix,.25*gaps-closing,4*gaps-closing)
    results=[];failures=[]
    for direction in (1,-1):
        order=np.arange(len(source))[::direction];points=source[order]
        edges=np.roll(points,-1,axis=0)-points
        bend=np.zeros_like(edges)
        if source_normals is not None:
            ns=np.asarray(source_normals)[order];next_ns=np.roll(ns,-1,axis=0)
            bend=.5*(ns*np.sum(edges*ns,axis=1)[:,None]-next_ns*np.sum(edges*next_ns,axis=1)[:,None])
        lengths=np.linalg.norm(edges,axis=1)
        if min(lengths)<=1e-12:raise ValueError('DegenerateSourceEyelid')
        lengths/=sum(lengths);knots=np.r_[0,np.cumsum(lengths)]
        def evaluate(t):
            u=np.mod(t,1.);indices=np.minimum(np.searchsorted(knots,u,side='right')-1,len(points)-1)
            f=(u-knots[indices])/lengths[indices]
            return points[indices]+f[:,None]*edges[indices]-(f*(1-f))[:,None]*bend[indices],indices,f
        def objective(t):
            hit,indices,f=evaluate(t);delta=hit-target
            derivative=edges[indices]-(1-2*f)[:,None]*bend[indices]
            return np.sum(delta*delta),2*np.sum(delta*derivative/lengths[indices,None],axis=1)
        phases=np.arange(256)/256
        phase=min(phases,key=lambda p:objective(start+p)[0])
        fit=minimize(objective,start+phase,jac=True,constraints=[constraint],method='SLSQP',
                     options={'ftol':1e-13,'maxiter':300})
        increments=matrix@fit.x+closing
        if not fit.success or np.any(increments<.25*gaps-1e-8) or np.any(increments>4*gaps+1e-8):
            failures.append(str(fit.message));continue
        hits,idx,f=evaluate(fit.x)
        # Return segment identities in the original source cycle direction.
        index=order[idx] if direction==1 else order[(idx+1)%len(order)]
        fraction=f if direction==1 else 1-f
        results.append((fit.fun,hits,index,fraction,{'winding':direction,
            'minimum_arc_ratio':float(np.min(increments/gaps)),
            'maximum_arc_ratio':float(np.max(increments/gaps)),
            'collapsed_intervals':int(np.sum(increments<=1e-10)),
            'source_windings':float(np.sum(increments)),
            'interpolation':'phong-alpha-0.5' if source_normals is not None else 'linear'}))
    if not results:raise ValueError('OrderedEyelidSolveIncomplete:'+'; '.join(failures))
    _,hits,index,fraction,diagnostics=min(results,key=lambda x:x[0])
    return hits,index,fraction,diagnostics
