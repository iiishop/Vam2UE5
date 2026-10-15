"""Non-runtime feasibility probe: independent canonical Neo-Hookean material and posed attachments.
No contact, no rendering, no acceptance substitution. Tests only whether this target can be realized without inverted cells.
"""
import json,time,sys
from pathlib import Path
import numpy as np
from scipy.optimize import minimize
s=json.loads(Path(sys.argv[1]).read_text());d=Path(s['audit_bundle']);top=json.loads((d/'cage-topology.json').read_text());parts=top['particles'];x=np.array(json.loads((d/'cage-canonical.json').read_text())['positions']);target=np.array(json.loads((d/'cage-shaped-reference.json').read_text())['positions']);tet=np.array(top['tetrahedra']);tet=tet[np.array([parts[i]['region']>=2 for i in tet[:,0]])];used=np.unique(tet);remap=np.full(len(x),-1);remap[used]=np.arange(len(used));t=remap[tet];x=x[used];target=target[used];parts=[parts[i] for i in used];n=len(x)
dm=np.transpose(x[t[:,1:]]-x[t[:,0,None]],(0,2,1));inv=np.linalg.inv(dm);vol=np.linalg.det(dm)/6
root=np.array([p['root_support'] for p in parts]);oldfixed=np.array([p['kinematic'] for p in parts]);dominant=np.array([max(p['influences'],key=lambda v:v[1])[0] for p in parts]);peak=np.array([max(v[1] for v in p['influences']) for p in parts]);fixed=oldfixed&(peak>.95)
# A bone core must not bridge differently driven articulated cores.
for cell in t:
 if len(set(dominant[cell]))>1:fixed[cell]=False
f=np.clip((root-.65)/.35,0,1);stiff=500*f*f*(3-2*f);mu=3000*.01/(2*1.45);lam=3000*.01*.45/(1.45*.1);alpha=1+mu/lam
free=np.flatnonzero(~fixed);z=target.copy();trace=[];start=time.time()
def energy(v):
 z[free]=v.reshape(-1,3);ds=np.transpose(z[t[:,1:]]-z[t[:,0,None]],(0,2,1));F=ds@inv;c0=np.cross(F[:,:,1],F[:,:,2]);c1=np.cross(F[:,:,2],F[:,:,0]);c2=np.cross(F[:,:,0],F[:,:,1]);cof=np.stack([c0,c1,c2],axis=2);J=np.einsum('ij,ij->i',F[:,:,0],c0)
 e=(vol*(mu/2*(np.sum(F*F,axis=(1,2))-3)+lam/2*(J-alpha)**2)).sum()+.5*np.sum(stiff[:,None]*(z-target)**2)
 P=mu*F+lam*(J-alpha)[:,None,None]*cof;H=(P@inv.transpose(0,2,1))*vol[:,None,None];g=stiff[:,None]*(z-target)
 for j in range(3):np.add.at(g,t[:,j+1],H[:,:,j]);np.add.at(g,t[:,0],-H[:,:,j])
 return float(e),g[free].ravel()
calls=0
def callback(v):
 global calls
 calls+=1
 if calls%50==0:
  z[free]=v.reshape(-1,3);det=np.linalg.det(np.transpose(z[t[:,1:]]-z[t[:,0,None]],(0,2,1))@inv);trace.append(dict(iteration=calls,inverted=int((det<=0).sum()),min_j=float(det.min()),seconds=time.time()-start));print(trace[-1],flush=True)
final_target=target.copy();z=x.copy()
for step in range(1,21):
 target=x+(final_target-x)*(step/20);z[fixed]=target[fixed]
 result=minimize(energy,z[free].ravel(),jac=True,method='L-BFGS-B',callback=callback,options={'maxiter':150,'ftol':1e-11,'gtol':1e-5,'maxls':40,'maxcor':15})
 z[free]=result.x.reshape(-1,3)
 print('POSE_STEP',step,flush=True)
z[free]=result.x.reshape(-1,3);J=np.linalg.det(np.transpose(z[t[:,1:]]-z[t[:,0,None]],(0,2,1))@inv);delta=np.linalg.norm(z-target,axis=1)
out=dict(pose=s['id'],success=bool(result.success),message=str(result.message),iterations=int(result.nit),old_fixed=int(oldfixed.sum()),shaft_fixed=int(fixed.sum()),inverted=int((J<=0).sum()),min_j=float(J.min()),volume_ratio=float(np.sum(vol*J)/sum(vol)),max_delta_cm=float(delta.max()),p95_delta_cm=float(np.percentile(delta,95)),seconds=time.time()-start,trace=trace)
Path(sys.argv[2]).write_text(json.dumps(out,indent=2));print(json.dumps(out,indent=2))
