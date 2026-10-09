"""B2: reconstruct sealed corrective parts with captured actual morph weights and LBS.
Usage: python report_corrective_diagnostics.py <B2 output> [--suite <pose suite>]
No pose interpolation is reimplemented here: all target/basis weights come from UE.
"""
import argparse,csv,html,json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.collections import PolyCollection
from PIL import Image,ImageDraw
from scipy.ndimage import binary_erosion,distance_transform_edt
p=argparse.ArgumentParser();p.add_argument('output',type=Path);p.add_argument('--suite',type=Path);a=p.parse_args();root=a.output.resolve();suite=a.suite or root/'poses'
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def points(p):return np.asarray(read(p)['positions'],dtype=float)
detail=read(root/'fields/detail.json');states=read(suite/'suite.json')['states'];cache={}
def field(name):
 if name not in cache:
  d=detail['fields'][name];cache[name]=np.fromfile(root/'fields'/d['file'],dtype='<f4').reshape(d['count'],d['components'])
 return cache[name]
parts=['source','procedural','residual','final','source_before','procedural_before','residual_before']
rois=['anterior_hip','lateral_glute','glute_thigh'];labels=['Anterior hip','Lateral glute','Glute-thigh transition']
def stats(v,w):
 ids=np.flatnonzero(w>.05)
 if not len(ids):return dict(max_cm=0,p95_cm=0,rms_cm=0,worst_render_vertex=-1)
 n=np.linalg.norm(v[ids],axis=1);return dict(max_cm=float(n.max()),p95_cm=float(np.percentile(n,95)),rms_cm=float(np.sqrt(np.average(n*n,weights=w[ids]))),worst_render_vertex=int(ids[n.argmax()]))
def project(v,state,view):
 cam=next(c for c in state['cameras'] if c['view']==view);m=np.asarray(state['body_to_world']).reshape(4,4)@np.linalg.inv(np.asarray(cam['world_matrix']).reshape(4,4));q=np.c_[v,np.ones(len(v))]@m;f=state['width']/(2*np.tan(np.deg2rad(cam['fov'])/2));return np.c_[state['width']/2+f*q[:,1]/q[:,0],state['height']/2-f*q[:,2]/q[:,0]],q[:,0]
def silhouette(xy,tri,w,h):
 im=Image.new('1',(w,h));d=ImageDraw.Draw(im)
 for t in tri:d.polygon([tuple(v) for v in xy[t]],fill=1)
 return np.asarray(im,dtype=bool)
def heat(ax,xy,z,tri,roi,value,title,vmax=1):
 use=np.max(roi[tri],axis=1)>.15;faces=tri[use];order=np.argsort(z[faces].mean(1))[::-1];faces=faces[order]
 coll=PolyCollection(xy[faces],array=value[faces].mean(1),cmap='magma',edgecolors='none',clim=(0,max(1e-8,vmax)),rasterized=True);ax.add_collection(coll)
 sel=xy[roi>.2];lo=sel.min(0)-3;hi=sel.max(0)+3;ax.set_xlim(lo[0],hi[0]);ax.set_ylim(hi[1],lo[1]);ax.set_aspect('equal');ax.set_title(title,fontsize=9);ax.axis('off');return coll
summary=[];target_rows=[];region_rows=[]
for state in states:
 name=state['id'];bundle=Path(state['audit_bundle']);manifest=read(bundle/'manifest.json');runtime=manifest.get('corrective_runtime')
 if not runtime:raise RuntimeError('Capture must contain actual G062 runtime driver values: '+name)
 if manifest['profiles']['corrective']['path']!=detail['profile']:raise RuntimeError('Profile identity mismatch')
 # Full data seal is verified in UE before export; render/input indices are captured from this same asset.
 inp=read(bundle/'03-corrective/inputs.json');mapping=read(bundle/'mapping.json');r2i=np.asarray(mapping['render_to_input'],dtype=int);n=len(r2i)
 bones=np.asarray([np.asarray(b['inverse_bind_matrix_row_major']).reshape(4,4)@np.asarray(b['component_matrix_row_major']).reshape(4,4) for b in inp['bones']]);linear=np.zeros((n,3,3))
 for i,row in enumerate(inp['render_vertex_influences']):
  for b,w in row:linear[i]+=bones[int(b),:3,:3]*w
 def skin(v):return np.einsum('ni,nij->nj',v,linear)
 baseline=points(bundle/'02-static-helpers/surface.json');actual=points(bundle/'03-corrective/surface.json');delta=actual-baseline;tri=np.asarray(inp['triangle_indices']).reshape(-1,3);used=np.zeros(n,bool);used[np.unique(tri)]=True
 # Deduplicate input aliases for numeric statistics, but keep all render indices in exported fields/plots.
 ids=np.asarray(mapping['input_to_source'])[r2i];ids=np.where(ids>=0,ids,-r2i-2);_,first=np.unique(ids,return_index=True);unique=np.zeros(n,bool);unique[first]=True;unique &=used
 aggregate={k:np.zeros((n,3)) for k in parts};per_side=[]
 for side in [0,1]:
  side_fields={k:np.zeros((n,3)) for k in parts};support=field(f's{side}_region_mask')[:,0][r2i];roi_fields={r:field(f's{side}_roi_{r}')[:,0][r2i] for r in rois}
  for target in detail['targets']:
   t=target['index'];weights=runtime['sides'][side]['target_weights'];tw=weights[t];item={k:np.zeros((n,3)) for k in parts};axis_weights=[]
   for basis in detail['bases']:
    if basis['side']!=side or basis['target']!=t:continue
    w=inp['active_morphs'].get(basis['morph'],0);axis_weights.append(float(w))
    if abs(w)<1e-12:continue
    for k in parts:item[k]+=field(f's{side}_t{t}_{k}_axis{basis["axis"]}')[r2i]*w
   for k in parts:side_fields[k]+=item[k]
   posed={k:skin(v) for k,v in item.items()}
   safety=field(f's{side}_t{t}_source_procedural_safety')[:,0][r2i] if t else np.ones(n)
   rsafety=field(f's{side}_t{t}_residual_safety')[:,0][r2i] if t else np.ones(n)
   for r,roi in roi_fields.items():
    w=roi*unique;pre=np.linalg.norm(posed['source_before'],axis=1)+np.linalg.norm(posed['procedural_before'],axis=1);rp=np.linalg.norm(posed['residual_before'],axis=1);active=(w>.05)&(pre>1e-9);ra=(w>.05)&(rp>1e-9)
    row=dict(pose=name,side=side,region=r,target=target['name'],target_weight=float(tw),actual_axis_weights='/'.join(f'{x:.8g}' for x in axis_weights),source_safety_min=float(safety[active].min()) if active.any() else None,source_safety_weighted_mean=float(np.average(safety[active],weights=w[active]*pre[active])) if active.any() else None,residual_safety_min=float(rsafety[ra].min()) if ra.any() else None,residual_safety_weighted_mean=float(np.average(rsafety[ra],weights=w[ra]*rp[ra])) if ra.any() else None)
    for k in parts:row[k+'_max_cm']=stats(posed[k],w)['max_cm']
    # Raw source diagnostic is in target-anchor space; report magnitude only, never add it to posed fields.
    row['raw_source_target_max_cm']=stats(field(f's{side}_t{t}_raw_source_local')[r2i],w)['max_cm'] if t else 0
    target_rows.append(row)
  posed={k:skin(v) for k,v in side_fields.items()}
  for k in parts:aggregate[k]+=posed[k]
  for r,roi in roi_fields.items():
   w=roi*unique;mask=w>.05;pre=np.linalg.norm(posed['source_before'],axis=1)+np.linalg.norm(posed['procedural_before'],axis=1)+np.linalg.norm(posed['residual_before'],axis=1)
   row=dict(pose=name,side=side,region=r,roi_vertices=int(mask.sum()),support_max=float(support[mask].max()) if mask.any() else 0,support_mean=float(np.average(support[mask],weights=w[mask])) if mask.any() else 0,coverage_fraction=float(np.average((support[mask]>.001).astype(float),weights=w[mask])) if mask.any() else 0)
   for k in parts:
    for stat,val in stats(posed[k],w).items():row[k+'_'+stat]=val
   for stat,val in stats(delta,w).items():row['actual_final_'+stat]=val
   lengths=sum(np.linalg.norm(posed[k],axis=1) for k in ['source','procedural','residual']);row['cancellation_fraction']=float(1-np.sum(np.linalg.norm(posed['final'],axis=1)*w)/max(1e-12,np.sum(lengths*w))) if np.sum(lengths*w)>1e-10 else 0
   region_rows.append(row)
  per_side.append(dict(fields=posed,support=support,rois=roi_fields))
 error=float(np.linalg.norm(aggregate['final']-delta,axis=1).max());sum_error=float(np.linalg.norm(aggregate['source']+aggregate['procedural']+aggregate['residual']-aggregate['final'],axis=1).max())
 if error>.001 or sum_error>.001:raise RuntimeError(f'{name}: parts do not match actual morph, error {error}, sum {sum_error}')
 out=root/name;out.mkdir(exist_ok=True);np.savez_compressed(out/'components.npz',**aggregate,actual_final=delta,baseline=baseline,actual_surface=actual,render_to_input=r2i,triangles=tri,**{f's{s}_{r}':v for s,d in enumerate(per_side) for r,v in d['rois'].items()},**{f's{s}_region_mask':d['support'] for s,d in enumerate(per_side)})
 # These are projected diagnostic meshes, not rendered screenshots. Units/color scales are explicit.
 fig,axes=plt.subplots(3,6,figsize=(18,10),layout='constrained');view='oblique';xy,z=project(actual,state,view)
 roi_union={r:np.maximum(per_side[0]['rois'][r],per_side[1]['rois'][r]) for r in rois};support=np.maximum(per_side[0]['support'],per_side[1]['support'])
 mag={k:np.linalg.norm(aggregate[k],axis=1) for k in parts};pre=sum(mag[k] for k in ['source_before','procedural_before','residual_before']);post=sum(mag[k] for k in ['source','procedural','residual']);loss=np.where(pre>1e-8,np.clip(1-post/np.maximum(pre,1e-8),0,1),0)
 scale=max(float(np.max(mag[k][np.maximum.reduce(list(roi_union.values()))>.05])) for k in ['source','procedural','residual','final']);scale=max(scale,.001)
 for ri,r in enumerate(rois):
  fields=[support,mag['source'],mag['procedural'],mag['residual'],np.linalg.norm(delta,axis=1),loss];titles=['Region mask (0..1)','Source (cm)','Procedural (cm)','Rotation residual (cm)','Actual final (cm)','Magnitude loss proxy (0..1)']
  for col,(val,title) in enumerate(zip(fields,titles)):
   coll=heat(axes[ri,col],xy,z,tri,roi_union[r],val,title,1 if col in [0,5] else scale);fig.colorbar(coll,ax=axes[ri,col],shrink=.65)
  axes[ri,0].text(0,.98,labels[ri],transform=axes[ri,0].transAxes,fontsize=10,va='top',bbox=dict(facecolor='white',alpha=.8))
 fig.suptitle(name+' | actual captured pose / oblique projection\nExact per-target multiplicative safety factors are in targets.csv; last column also includes cancellation effects.',fontsize=12);fig.savefig(out/'fields.png',dpi=125);plt.close(fig)
 contours={};fig,axes=plt.subplots(1,4,figsize=(15,4.6),layout='constrained')
 for ax,view in zip(axes,['side','rear','front','oblique']):
  before,_=project(baseline,state,view);after,_=project(actual,state,view);w,h=state['width'],state['height'];bm=silhouette(before,tri,w,h);fm=silhouette(after,tri,w,h);be=bm&~binary_erosion(bm);fe=fm&~binary_erosion(fm);distb=distance_transform_edt(~be);distf=distance_transform_edt(~fe)
  union=np.maximum.reduce(list(roi_union.values()));v=after[union>.15];lo=np.floor(v.min(0)-8).astype(int);hi=np.ceil(v.max(0)+8).astype(int);lo=np.maximum(lo,0);hi=np.minimum(hi,[w,h]);region=np.zeros((h,w),bool);region[lo[1]:hi[1],lo[0]:hi[0]]=True
  contours[view]={'changed_pixels_roi':int(np.count_nonzero((bm^fm)&region)),'boundary_max_px_roi':float(max(np.max(distb[fe&region],initial=0),np.max(distf[be&region],initial=0))),'roi_bounds_pixels':[lo.tolist(),hi.tolist()],'method':'Triangle union rasterized in captured camera, 768px; geometric silhouette, not UE shadow/normal map.'}
  ax.contour(bm,levels=[.5],colors=['#009eb8'],linewidths=1);ax.contour(fm,levels=[.5],colors=['#db2670'],linewidths=1);ax.set_xlim(lo[0],hi[0]);ax.set_ylim(hi[1],lo[1]);ax.set_aspect('equal');ax.axis('off');ax.set_title(f'{view}: {contours[view]["boundary_max_px_roi"]:.2f} px max')
 fig.suptitle(name+' | cyan: static helpers / pink: corrective applied\nSame actual bones, shape and camera. Geometric contour comparison.');fig.savefig(out/'contours.png',dpi=140);plt.close(fig)
 record=dict(pose=name,actor=manifest['actor'],configuration=manifest['configuration']['path'],profile=detail['profile'],runtime=runtime,reconstruction_max_cm=error,sum_max_cm=sum_error,contours=contours,audit_bundle=str(bundle),contact_status=state['contact_status']);(out/'diagnostics.json').write_text(json.dumps(record,indent=2));summary.append(record);print(name,'max replay cm',error,flush=True)
def csvwrite(name,rows):
 with (root/name).open('w',newline='',encoding='utf-8-sig') as f:
  w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
csvwrite('targets.csv',target_rows);csvwrite('regions.csv',region_rows);(root/'summary.json').write_text(json.dumps({'rebake_max_error_cm':detail['rebake_max_error_cm'],'states':summary,'regions':region_rows},indent=2))
style='body{background:#171b20;color:#e9edf1;font:16px system-ui;max-width:1550px;margin:28px auto;padding:0 24px}a{color:#83d9f1}img{max-width:100%}table{border-collapse:collapse;font-size:13px}td,th{border:1px solid #56616b;padding:6px}details{margin:24px 0}code{color:#f3c682}.warning{background:#48352b;padding:16px}.shot{max-width:360px}'
page=[f'<!doctype html><meta charset="utf-8"><title>B2 corrective attribution</title><style>{style}</style><h1>B2 · 修形为什么没起作用</h1>', '<p>复用现有 G062 swing–twist、RBF 权重和 rotation-blend-minus-LBS residual。分项由最终权重重新烘焙导出，再用角色实际激活的 morph 权重、蒙皮矩阵重放；不根据 UI 欧拉角猜测。</p>',f'<p>重烘焙与封存 B1 目标最大差：{detail["rebake_max_error_cm"]:.4g} cm。分项合计与实际最终 morph 最大差：{max(x["reconstruction_max_cm"] for x in summary):.6g} cm。</p>', '<p><a href="targets.csv">全部目标权重／逐区域 safety CSV</a> · <a href="regions.csv">局部极值／覆盖率 CSV</a> · <a href="fields/detail.json">逐顶点原始字段与数据摘要</a> · <a href="summary.json">完整结果</a> · <a href="poses/compare.html">真实 UE 截图</a></p>', '<div class="warning">130° 组合等为合成安全探针，不是 VaM 真实形态目标。插值目标也不是每个姿态的 VaM 独立扫描：源项来自已验证 Bend Fix 映射／thigh-X 方程，叠加程序项和蒙皮 residual。检查区域是工程 ROI，不代表已验证的解剖分区。</div>', '<p>读图：region mask 小＝覆盖不足；target weight 小＝姿态混合贡献小；safety 接近 0＝烘焙被安全约束压低；分项较大但最终较小＝方向抵消。最终位移不等于外轮廓变化，内部移动或相机遮挡可使轮廓基本不变。热图末列是幅值损失代理，精确 safety 请看下表/CSV。</p>']
seat=next((x for x in summary if x['pose']=='suspended-seat'),None)
if seat:
 page.append('<h2>坐姿定位摘要</h2><p>左右 Flex90 权重：'+str([round(x['target_weights'][3],8) for x in seat['runtime']['sides']])+'. 查看髋前覆盖率与臀外侧分项：目标被选中不代表源修形覆盖了问题区域。源项缺失无法用放大 RBF 权重补足；rotation residual 也不等于解剖形态目标。</p>')
 page.append('<table><tr><th>侧／工程 ROI</th><th>mask 均值</th><th>覆盖比例</th><th>源 max cm</th><th>程序 max cm</th><th>残差 max cm</th></tr>')
 for x in region_rows:
  if x['pose']=='suspended-seat':page.append('<tr><td>'+str(x['side'])+'/'+x['region']+'</td>'+''.join('<td>'+format(x[k],'.5g')+'</td>' for k in ['support_mean','coverage_fraction','source_max_cm','procedural_max_cm','residual_max_cm'])+'</tr>')
 page.append('</table><p>覆盖率为工程 ROI 加权后 mask > 0.001 的比例，非解剖覆盖百分比。未修改物理或形态参数。</p>')
for rec in summary:
 name=rec['pose'];page.append(f'<details {"open" if name=="suspended-seat" else ""}><summary><strong>{html.escape(name)}</strong> · replay {rec["reconstruction_max_cm"]:.4g} cm</summary>')
 page.append(f'<p><a href="{name}/components.npz">分项表面与逐顶点位移 NPZ</a> · <a href="{name}/diagnostics.json">实例／权重／轮廓</a></p>')
 page.append(f'<img class="shot" src="poses/{name}/side-gray.png" alt="UE真实截图"><img src="{name}/contours.png"><img src="{name}/fields.png">')
 page.append('<h3>各目标实际权重（原生 G062）</h3><table><tr><th>目标</th><th>左</th><th>右</th></tr>')
 for t in detail['targets']:page.append(f'<tr><td>{t["name"]}</td><td>{rec["runtime"]["sides"][0]["target_weights"][t["index"]]:.6g}</td><td>{rec["runtime"]["sides"][1]["target_weights"][t["index"]]:.6g}</td></tr>')
 page.append('</table><h3>局部贡献与安全衰减（仅显示有贡献的目标，全部目标见 CSV）</h3><table><tr><th>侧／区域／目标</th><th>源 cm</th><th>程序 cm</th><th>残差 cm</th><th>源/程序 safety 均值</th><th>残差 safety 均值</th><th>最终 cm</th></tr>')
 for row in target_rows:
  if row['pose']!=name or row['target_weight']<.001:continue
  fmt=lambda x:'—' if x is None else f'{x:.4g}'
  page.append('<tr><td>'+f'{row["side"]}/{row["region"]}/{row["target"]}'+'</td>'+''.join('<td>'+fmt(row[k])+'</td>' for k in ['source_max_cm','procedural_max_cm','residual_max_cm','source_safety_weighted_mean','residual_safety_weighted_mean','final_max_cm'])+'</tr>')
 page.append('</table></details>')
page.append('<details><summary>全部安全探针（不作为 VaM 形态真值）</summary><pre>'+html.escape(json.dumps(detail['safety_probes'],indent=2))+'</pre></details>');import os
suite_link=os.path.relpath(suite,root).replace('\\','/')
(root/'compare.html').write_text(''.join(page).replace('href="poses/','href="'+suite_link+'/').replace('src="poses/','src="'+suite_link+'/'),encoding='utf-8')

# Regional contour plots use saved arrays, not an additional UE simulation.
import subprocess,sys
subprocess.run([sys.executable,str(Path(__file__).with_name("report_corrective_contours.py")),str(root),str(suite.resolve())],check=True)
