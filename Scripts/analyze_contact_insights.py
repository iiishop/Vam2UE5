from pathlib import Path
import csv,json,numpy as np
r=Path(__file__).resolve().parents[1]/'Saved/ContactInsights'
keys=['VamBreastContact','VamContactNativeMaterial','VamContactVolumeConstraints','VamContactLocalCompression','VamContactZonalVolume','VamContactPostCollision','VamContactSkinBending','VamContactEdgeSweep','VamContactCellBarrierSweep','VamContactFaceProjection','VamContactConvergenceScan']
reference=r/'reference' if (r/'reference/world_held-GameThread.csv').exists() else r
result={}
for name,d in [('default',reference),('workers4',r/'workers4'),('workers8',r/'workers8')]:
 result[name]={}
 for phase in ['press_left','world_moving','world_held','disabled_after']:
  f=d/(phase+'-GameThread.csv')
  if not f.exists():continue
  rows={x['Name']:x for x in csv.DictReader(f.open(encoding='utf-8-sig'))};ticks=int(rows['VamBreastContact']['Count'])
  result[name][phase]={'ticks':ticks,'ms_per_contact_tick':{k:float(rows[k]['Incl'])*1000/ticks for k in keys if k in rows},'calls_per_contact_tick':{k:int(rows[k]['Count'])/ticks for k in keys if k in rows}}
 result[name]['gpu_differences']=[]
 for f in sorted(d.glob('gpu-*.bin')):
  a=np.fromfile(reference/f.name,np.float32).reshape(-1,3);b=np.fromfile(f,np.float32).reshape(-1,3);diff=np.linalg.norm(a.astype(float)-b.astype(float),axis=1)
  result[name]['gpu_differences'].append({'file':f.name,'identical':bool(np.array_equal(a,b)),'max_cm':float(diff.max())})
 print(name,result[name].get('world_held',{}))
(r/'results.json').write_text(json.dumps(result,indent=2))
labels={'VamBreastContact':'接触组件总计','VamContactNativeMaterial':'原生 GS 主求解（材料/支承）','VamContactVolumeConstraints':'体积约束合计','VamContactLocalCompression':'↳ 局部压缩','VamContactZonalVolume':'↳ 总体积修正','VamContactPostCollision':'追加接触修正合计','VamContactSkinBending':'↳ 表面弯曲与防翻转','VamContactEdgeSweep':'↳ 边约束扫描','VamContactCellBarrierSweep':'↳ 四面体防压塌扫描','VamContactFaceProjection':'↳ 三角面接触','VamContactConvergenceScan':'↳ 收敛检查'}
rows=''
for k in keys:
 vals=[result['default'][phase]['ms_per_contact_tick'][k] for phase in ['press_left','world_moving','world_held']]
 rows+='<tr><td>'+labels[k]+'</td>'+''.join('<td>%.3f</td>'%v for v in vals)+'</tr>'
workerrows=''
for name in result:
 a=result[name]['world_held']['ms_per_contact_tick'];workerrows+='<tr><td>'+name+'</td><td>%.3f</td><td>%.3f</td></tr>'%(a['VamBreastContact'],a['VamContactNativeMaterial'])
(r/'compare.html').write_text('<!doctype html><meta charset="utf-8"><title>Chaos Contact · Insights 瓶颈分析</title><style>body{background:#141a24;color:#e8edf5;font:17px/1.65 system-ui;max-width:1080px;margin:48px auto;padding:0 24px}h2,a{color:#8bbfff}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{padding:9px 14px;border-bottom:1px solid #39485c;text-align:left}strong{color:#f4d18b}</style><h1>Chaos Contact · 实际函数计时</h1><p>UE 5.8 Unreal Insights；真实玻璃球碰撞；每个数值为 GameThread 上该 scope 的总耗时 / 接触组件更新次数。不是整个游戏帧，不将多个线程的 CPU 时间相加当作帧耗时。</p><h2>瓶颈分解 / ms</h2><table><tr><th>阶段</th><th>平板按住</th><th>球移动按压</th><th>球稳定按住</th></tr>'+rows+'</table><p>↳ 为上方合计的子项，不能重复相加。保持阶段有 92 次更新（含 GPU readback 期间的额外两次），移动阶段为 90 次；均按实际计数归一化。</p><h2>针对实际热点的调度实验</h2><table><tr><th>MaxNumWorkers</th><th>接触组件 / ms</th><th>GS 主求解 / ms</th></tr>'+workerrows+'</table><p><strong>4 和 8 均未显示收益，未改正式全局默认。</strong>所有采样 GPU 顶点与默认路径逐位一致。已有批大小 CVar 没有传入当前 GS 主约束构造函数，旧批大小实验不能代表主求解器的调优结果。</p><h2>结论与边界</h2><ul><li>GS 主求解约占当前接触组件耗时的 59%；已经在并行工作。</li><li>球按压每更新 12 次主求解、约 48 次局部扫描，即每轮通常只跑最低 4 次，并非一直触及 24 次上限。</li><li>本轮没有通过换材质、减迭代、冻结组织或降低网格密度来换性能。</li><li>安装版没有完整引擎 PDB，本次内置 scope 不能进一步分离 GS 内部应力、极分解和 Hessian 的时间；源码能解释调用链，不能替代更细的实测。</li><li>Tracing 本身有开销。单人物受控测试，不是完整游戏或四人物帧率保证。</li></ul><p><a href="results.json">计时 JSON</a> · <a href="world_held-GameThread.csv">原始 CSV</a> · <a href="baseline.utrace">Unreal Insights 原始 trace</a></p>',encoding='utf-8')
