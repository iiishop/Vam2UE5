"""Audit real UE-exported tetrahedra; optional offline material compression study.

The material experiment is NOT Chaos and is NOT a rendered character test.
It uses identical stable Neo-Hookean energies on the exported UE meshes.
"""
import argparse
import json
from pathlib import Path
import numpy as np
from scipy.optimize import minimize
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d.art3d import Poly3DCollection


def matrices(v, t):
    p = v[t]
    return np.stack((p[:, 1]-p[:, 0], p[:, 2]-p[:, 0], p[:, 3]-p[:, 0]), axis=2)


def boundary(t):
    faces = np.concatenate([t[:, f] for f in ([1,2,3],[0,3,2],[0,1,3],[0,2,1])])
    _, idx, counts = np.unique(np.sort(faces, axis=1), axis=0, return_index=True, return_counts=True)
    return faces[idx[counts == 1]]


def load(path):
    d = json.loads(path.read_text(encoding='utf-8-sig'))
    v, t = np.array(d['vertices'],float), np.array(d['tets'],int)
    det = np.linalg.det(matrices(v,t))
    neg = det < 0
    t[neg, 2], t[neg, 3] = t[neg, 3].copy(), t[neg, 2].copy()
    return d,v,t


def audit(d,v,t):
    p=v[t]; vol=np.linalg.det(matrices(v,t))/6
    edges=np.stack([p[:,j]-p[:,i] for i in range(4) for j in range(i+1,4)],axis=1)
    # Mean-ratio quality: 1 for a regular tet, tends to 0 for degeneracy.
    q=12*np.power(3*np.abs(vol),2/3)/np.square(edges).sum(axis=(1,2))
    incidence=np.bincount(t.ravel(),minlength=len(v))
    return dict(method=d['method'],side=d['side'],particles=len(v),tets=len(t),
                volume_cm3=float(vol.sum()),quality_min=float(q.min()),quality_p05=float(np.quantile(q,.05)),
                quality_median=float(np.median(q)),low_quality_fraction=float((q<.1).mean()),
                maximum_node_incidence=int(incidence.max()),degenerate=int((vol<1e-10).sum()),
                generation_seconds=d['generation_seconds'])


def compress(v,t,zonal):
    # Prescribed axial compression along laboratory Z; lateral DOFs remain free.
    # This axis is for this isolated experiment, not an anatomical runtime rule.
    scale=np.ptp(v,axis=0).max(); rest=(v-v.min(axis=0))/scale
    inv=np.linalg.inv(matrices(rest,t)); vols=np.linalg.det(matrices(rest,t))/6
    total=vols.sum(); height=np.ptp(rest[:,2]); x=rest.copy()
    mu,lam,k=1.,2.,(1000. if zonal else 0.)
    def energy(flat):
        x=flat.reshape(-1,3); f=matrices(x,t)@inv
        co=np.stack((np.cross(f[:,:,1],f[:,:,2]),np.cross(f[:,:,2],f[:,:,0]),np.cross(f[:,:,0],f[:,:,1])),axis=2)
        j=np.einsum('ni,ni->n',f[:,:,0],co[:,:,0]); dv=np.dot(vols,j)/total-1
        e=np.dot(vols,.5*mu*(np.square(f).sum(axis=(1,2))-3)-mu*(j-1)+.5*lam*(j-1)**2)+.5*k*total*dv*dv
        # Differentiable inversion barrier; failed outcomes are reported, not hidden.
        deficit=np.minimum(j-.1,0);e+=np.dot(vols,500*deficit**2)
        stress=mu*f+(lam*(j-1)-mu+k*dv+1000*deficit)[:,None,None]*co
        h=(stress@inv.transpose(0,2,1))*vols[:,None,None]
        g=np.zeros_like(x)
        for c in range(3):np.add.at(g,t[:,c+1],h[:,:,c])
        np.add.at(g,t[:,0],-h.sum(axis=2))
        return e,g.ravel()
    # Check the analytic derivative independently before interpreting a solve.
    probe=rest.copy();probe[:,2]*=.95
    direction=np.random.default_rng(0).normal(size=probe.size);direction/=np.linalg.norm(direction)
    h=1e-6;e,g=energy(probe.ravel())
    fd=(energy(probe.ravel()+h*direction)[0]-energy(probe.ravel()-h*direction)[0])/(2*h)
    assert abs(fd-np.dot(g,direction))<1e-5*max(1,abs(fd)), 'Energy derivative mismatch'
    stages=[]
    for ratio in (.96,.92,.88,.84,.80):
        x[:,2]=rest[:,2]*ratio
        # Fix axial coordinates to exclude rigid-body rotation escaping the load.
        # This is a constitutive diagnostic, not a sphere/plane contact simulation.
        bounds=[b for z in x[:,2] for b in ((None,None),(None,None),(z,z))]
        result=minimize(energy,x.ravel(),jac=True,method='L-BFGS-B',bounds=bounds,
                        options=dict(maxiter=1200,ftol=1e-11,gtol=1e-7,maxls=40,maxcor=15))
        x=result.x.reshape(-1,3)
        stages.append(dict(height_ratio=ratio,success=bool(result.success),iterations=result.nit,message=str(result.message)))
    j=np.linalg.det(matrices(x,t)@inv)
    return x*scale+v.min(axis=0),dict(volume_ratio=float(np.dot(vols,j)/total),
        width_x_ratio=float(np.ptp(x[:,0])/np.ptp(rest[:,0])),width_y_ratio=float(np.ptp(x[:,1])/np.ptp(rest[:,1])),
        min_j=float(j.min()),inverted=int((j<=0).sum()),stages=stages)


def draw(ax,v,faces,title,bounds):
    obj=Poly3DCollection(v[faces],facecolors='#70aaca',edgecolors='#294452',linewidths=.15,alpha=.95)
    ax.add_collection3d(obj);ax.set_title(title,fontsize=9)
    for setter,i in ((ax.set_xlim,0),(ax.set_ylim,1),(ax.set_zlim,2)):setter(*bounds[:,i])
    ax.set_box_aspect(np.ptp(bounds,axis=0));ax.view_init(elev=12,azim=-65);ax.set_axis_off()


def main():
    ap=argparse.ArgumentParser();ap.add_argument('directory',type=Path);ap.add_argument('--compression',action='store_true');a=ap.parse_args()
    meshes=[];rows=[]
    for p in sorted(a.directory.glob('*-[01].json')):
        d,v,t=load(p);rows.append(audit(d,v,t));meshes.append((d,v,t))
    (a.directory/'quality.json').write_text(json.dumps(rows,indent=2))
    selected=[m for m in meshes if m[0]['side']==0]
    fig=plt.figure(figsize=(4*len(selected),5))
    for i,(d,v,t) in enumerate(selected):
        # Remove cells on one side to expose internal connectivity.
        cut=t[v[t].mean(axis=1)[:,1]>np.median(v[:,1])]
        bounds=np.array([v.min(axis=0),v.max(axis=0)])
        draw(fig.add_subplot(1,len(selected),i+1,projection='3d'),v,boundary(cut),d['method']+'\ninternal cutaway',bounds)
    fig.suptitle('Actual UE 5.8 tetrahedra | identical input boundary | NOT pressure results')
    fig.tight_layout();fig.savefig(a.directory/'mesh-comparison.png',dpi=160);plt.close(fig)
    if a.compression:
        records=[];selected=[m for m in selected if m[0]['method'] in ('layered','iso6','tetwild15')]
        fig=plt.figure(figsize=(12,4*len(selected)))
        for row,(d,v,t) in enumerate(selected):
            faces=boundary(t);states=[v];titles=['Rest']
            for zonal in (False,True):
                x,info=compress(v,t,zonal);states.append(x)
                records.append(dict(method=d['method'],zonal=zonal,**info))
                titles.append(('Zonal volume' if zonal else 'Compressible')+f"\nV/V0={info['volume_ratio']:.3f}, width={info['width_x_ratio']:.3f}")
                print(records[-1],flush=True)
            vv=np.concatenate(states);bounds=np.array([vv.min(axis=0),vv.max(axis=0)])
            for col,(x,title) in enumerate(zip(states,titles)):
                draw(fig.add_subplot(len(selected),3,row*3+col+1,projection='3d'),x,faces,d['method']+' | '+title,bounds)
        fig.suptitle('OFFLINE material study, NOT Chaos/contact | 20% prescribed axial compression | identical shear modulus')
        fig.tight_layout();fig.savefig(a.directory/'volume-study.png',dpi=160)
        (a.directory/'compression.json').write_text(json.dumps(records,indent=2))
    table=''.join(f"<tr><td>{r['method']}</td><td>{r['side']}</td><td>{r['particles']}</td><td>{r['tets']}</td><td>{r['quality_p05']:.4f}</td><td>{r['volume_cm3']:.2f}</td></tr>" for r in rows)
    page='''<!doctype html><meta charset="utf-8"><title>四面体与体积保持对比</title>
    <style>body{font:17px system-ui;max-width:1400px;margin:32px auto;padding:24px;background:#161b22;color:#edf3f8}img{width:100%;background:white}td,th{padding:8px 20px;text-align:left}a{color:#82caff}</style>
    <h1>UE 5.8 原生四面体生成对比</h1>
    <p>同一人物两侧、相同输入边界。网格由 UE 原生算法实际生成；下方材料试验为离线参考，非 Chaos Runtime 或人物按压截图。当前 BP 未替换。</p>
    <h2>内部网格剖视</h2><img src="mesh-comparison.png">
    <p>quality P05：四面体 mean-ratio 质量的第5百分位，1为正四面体，趋近0表示退化。体积是碰撞代理几何体积，非医学体积。</p>
    <table><tr><th>方法</th><th>侧</th><th>点</th><th>四面体</th><th>质量P05</th><th>初始体积 cm³</th></tr>'''+table+'''</table>
    <h2>保持体积时，压扁必须伴随横向扩张</h2><p>下面固定各顶点轴向压缩20%，横向由能量最小化自由求解；不是球体碰撞。左右列分别为原状、可压缩材料、增加整体体积约束。无皮肤层、无骨骼附着。</p>
    <img src="volume-study.png"><p>这一试验验证材料响应和网格数值条件，不能证明人物上的三角压痕已经解决。完整收敛状态在 JSON 中。</p>
    <p><a href="quality.json">网格指标</a> · <a href="compression.json">材料试验结果</a> · <a href="run.log">UE 原生测试日志</a></p>'''
    (a.directory/'compare.html').write_text(page,encoding='utf-8')
    print(json.dumps(rows,indent=2))


if __name__=='__main__':main()
