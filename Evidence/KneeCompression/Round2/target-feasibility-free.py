"""Offline feasibility experiment, not runtime or visual acceptance.
Project per-cell target Jacobians to positive determinant, then integrate globally.
All source matrices and target positions come from an actual UE A1 capture.
"""
import json,sys,time
from pathlib import Path
import numpy as np
from scipy.sparse import coo_matrix,diags
from scipy.sparse.linalg import factorized
sf=Path(sys.argv[1]);out=Path(sys.argv[2]);s=json.loads(sf.read_text());d=Path(s['audit_bundle']);top=json.loads((d/'cage-topology.json').read_text());x=np.array(json.loads((d/'cage-canonical.json').read_text())['positions']);y=np.array(json.loads((d/'cage-shaped-reference.json').read_text())['positions']);p=top['particles'];t=np.array(top['tetrahedra']);t=t[np.array([p[i]['region']>=2 for i in t[:,0]])];used=np.unique(t);idx=np.full(len(x),-1);idx[used]=np.arange(len(used));t=idx[t];x=x[used];y=y[used];n=len(x);nt=len(t)
# F = Ds Dm^-1; three coordinate-independent gradient rows per tetrahedron.
dm=np.transpose(x[t[:,1:]]-x[t[:,0,None]],(0,2,1));inv=np.linalg.inv(dm);vol=np.linalg.det(dm)/6
grad=np.concatenate((-inv.sum(axis=1)[:,None,:],inv),axis=1)
rows=np.repeat(np.arange(nt*3),4);cols=np.repeat(t[:,None,:],3,axis=1).ravel();values=grad.transpose(0,2,1).ravel();G=coo_matrix((values,(rows,cols)),shape=(nt*3,n)).tocsr();w=np.repeat(vol,3);L=G.T@diags(w)@G
kin=np.array([p[i]['kinematic'] for i in used]);records=[]
for mode in ['free-inner']:
 a=np.full(n,.01);a[kin]=.01
 solve=factorized((L+diags(a)).tocsc());z=y.copy();start=time.time()
 for it in range(300):
  F=(G@z).reshape(nt,3,3).transpose(0,2,1);u,sv,vh=np.linalg.svd(F);sign=np.linalg.det(u@vh);u[:,:,-1]*=sign[:,None];sv[:,-1]=np.maximum(.15,sv[:,-1]*sign);desired=(u*sv[:,None,:])@vh;rhs=G.T@(desired.transpose(0,2,1).reshape(-1,3)*w[:,None])+a[:,None]*y
  z=np.column_stack([solve(rhs[:,j]) for j in range(3)])
  if it in [0,49,99,299]:
   F=(G@z).reshape(nt,3,3);det=np.linalg.det(F);delta=np.linalg.norm(z-y,axis=1);records.append(dict(mode=mode,iteration=it+1,inverted=int((det<=0).sum()),min_j=float(det.min()),max_displacement_cm=float(delta.max()),p95_displacement_cm=float(np.percentile(delta,95)),inner_max_cm=float(delta[kin].max()),seconds=time.time()-start))
print(json.dumps(records,indent=2));out.write_text(json.dumps(records,indent=2))
