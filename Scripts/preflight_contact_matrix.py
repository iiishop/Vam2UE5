"""Matrix preflight only. Not a contact solver or an FPS prediction."""
import os
os.environ['OPENBLAS_NUM_THREADS']='1'
os.environ['OMP_NUM_THREADS']='1'
from pathlib import Path
import numpy as np,scipy.sparse as sp,scipy.sparse.linalg as sla
import json,time,statistics
root=Path(__file__).resolve().parents[1]/'Saved/ContactPrecompute'
data=json.loads((root/'mesh.json').read_text());v=np.array(data['vertices']);t=np.array(data['tets']);fixed=np.array(data['fixed'],bool)
start=time.perf_counter()
dm=np.stack([v[t[:,i]]-v[t[:,0]] for i in [1,2,3]],axis=2)
inv=np.linalg.inv(dm);vol=np.abs(np.linalg.det(dm))/6
# Positive scalar finite-element stretching operator; three coordinate RHS.
# Consistent with a PD-style global stretch block, not full native GS Hessian.
g=np.concatenate([-inv.sum(axis=1)[:,None,:],inv],axis=1)
mu=data['young_pa']*.01/(2*(1+data['nu']))
ke=2*mu*vol[:,None,None]*(g@g.transpose(0,2,1))
rows=np.broadcast_to(t[:,:,None],ke.shape).ravel();cols=np.broadcast_to(t[:,None,:],ke.shape).ravel()
k=sp.coo_matrix((ke.ravel(),(rows,cols)),shape=(len(v),len(v))).tocsc()
free=np.flatnonzero(~fixed);a=k[free][:,free].tocsc()
assembly=(time.perf_counter()-start)*1000
start=time.perf_counter();factor=sla.splu(a,permc_spec='MMD_AT_PLUS_A');factor_ms=(time.perf_counter()-start)*1000
rng=np.random.default_rng(29);rhs=np.asfortranarray(rng.normal(size=(len(free),3)))
timings=[]
for i in range(201):
 start=time.perf_counter();x=factor.solve(rhs);elapsed=(time.perf_counter()-start)*1000
 if i:timings.append(elapsed)
res=float(np.linalg.norm(a@x-rhs)/np.linalg.norm(rhs))
def storage(m):return int(m.data.nbytes+m.indices.nbytes+m.indptr.nbytes)
surface=np.unique(np.array(data['surface']).ravel());free_surface=np.intersect1d(free,surface)
result={'scope':'Only fixed scalar stretching matrix, sparse LU factorization and 3-RHS backsolve. No local rotations, volume/contact solve, rendering or native GS equivalence.','nodes':len(v),'tets':len(t),'free_nodes':len(free),'free_surface_nodes':len(free_surface),'surface_fraction_of_free':len(free_surface)/len(free),'assembly_ms':assembly,'factor_ms':factor_ms,'matrix_nonzeros':a.nnz,'matrix_bytes':storage(a),'factor_bytes':storage(factor.L)+storage(factor.U)+factor.perm_r.nbytes+factor.perm_c.nbytes,'backsolve_median_ms':statistics.median(timings),'backsolve_p95_ms':sorted(timings)[190],'linear_relative_residual':res,'dense_surface_schur_float64_bytes':int(len(free_surface)**2*8),'fixed_nodes':int(fixed.sum())}
(root/'matrix-preflight.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
assert res<1e-8 and fixed.any()
