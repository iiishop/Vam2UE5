"""Report native exact cache and matrix preflight; never equate matrix timing to FPS."""
from pathlib import Path
import csv,json,re,statistics,numpy as np
root=Path(__file__).resolve().parents[1]/'Saved';out=root/'ContactPrecompute';out.mkdir(exist_ok=True)
result={'single':{},'double':{},'matrix':json.loads((out/'matrix-preflight.json').read_text())}
for name in ['production','geometry','memo','cache-final']:
 d=root/'ContactInsights'/name
 if not (d/'world_held-GameThread.csv').exists():continue
 r={'phases':{},'gpu':[],'test':json.loads((d/'summary.json').read_text())}
 for phase in ['world_moving','world_held']:
  rows={x['Name']:x for x in csv.DictReader((d/(phase+'-GameThread.csv')).open(encoding='utf-8-sig'))};n=int(rows['VamBreastContact']['Count'])
  r['phases'][phase]={k:float(rows[k]['Incl'])*1000/n for k in ['VamBreastContact','VamContactNativeMaterial']}
 for f in d.glob('gpu-*.bin'):
  a=np.fromfile(root/'ContactInsights'/f.name,np.float32).reshape(-1,3);b=np.fromfile(f,np.float32).reshape(-1,3)
  r['gpu'].append({'file':f.name,'exact':bool(np.array_equal(a,b)),'max_cm':float(np.linalg.norm(a.astype(float)-b.astype(float),axis=1).max())})
 lines=[l for l in (d/'runtime.log').read_text(errors='replace').splitlines() if ']LogTemp:' in l]
 pairs=[tuple(map(int,m)) for m in re.findall(r'hits=(\d+) calls=(\d+)','\n'.join(lines))]
 if pairs:r['hit_fraction']=sum(a for a,b in pairs)/sum(b for a,b in pairs)
 r['allocated_bytes']=sorted(set(map(int,re.findall(r'bytes=(\d+)','\n'.join(lines)))))
 result['single'][name]=r
for name in ['cache-control','cache-exact']:
 rows=list(csv.DictReader((root/'ContactTwoCharacters'/name/'frames.csv').open()));r={}
 for phase in dict.fromkeys(x['phase'] for x in rows):
  selected=[x for x in rows if x['phase']==phase];v=sorted(float(x['frame_ms']) for x in selected)
  r[phase]={'mean_ms':statistics.mean(v),'fps':1000/statistics.mean(v),'p95_ms':v[int(len(v)*.95)],'solvers':sorted(set(int(x['active_solvers']) for x in selected))}
 result['double'][name]=r
(out/'results.json').write_text(json.dumps(result,indent=2))
rows=''
for name,r in result['single'].items():
 h=r['phases']['world_held'];m=r['phases']['world_moving'];rows+=f'<tr><td>{name}</td><td>{h["VamBreastContact"]:.3f}</td><td>{h["VamContactNativeMaterial"]:.3f}</td><td>{m["VamBreastContact"]:.3f}</td><td>{all(x["exact"] for x in r["gpu"])}</td></tr>'
dual=''
for phase in ['on_pressed','on_moving']:
 dual+='<tr><td>'+phase+'</td>'
 for name in ['cache-control','cache-exact']:
  r=result['double'][name][phase];dual+=f'<td>{r["mean_ms"]:.2f} ms / {r["fps"]:.2f} FPS</td>'
 dual+='</tr>'
m=result['matrix']
html='''<!doctype html><meta charset="utf-8"><title>Chaos 空间换时间实验</title><style>body{background:#151b25;color:#e5ecf7;font:17px/1.7 system-ui;max-width:1150px;margin:40px auto;padding:0 24px}h2,a{color:#8fcaff}table{border-collapse:collapse;width:100%}th,td{padding:10px;border-bottom:1px solid #3e4b60;text-align:left}strong{color:#f5cd87}</style><h1>空间换时间：实际缓存与预分解试验</h1><p>2026-10-09。没有减少四面体、迭代、体积约束、按压深度或 Jiggle 幅度。当前仍未达到双人受压 60 FPS。</p><h2>原生完整运行路径</h2><p>production 为此前默认对照；geometry 只预计算静态系数；memo 加入形变矩阵数值完全一致时的应力缓存；cache-final 为缓存模式 2 的最终候选复测。数据是单人物接触组件耗时，不是整帧。</p><table><tr><th>方案</th><th>稳定受压总 ms</th><th>其中 GS ms</th><th>移动总 ms</th><th>四个 GPU 快照逐位一致</th></tr>'''+rows+'''</table><p>精确缓存约 1.14 MiB / 人物，首轮完整测试命中率约 45.4%。实例不共享动态缓存；每个四面体依据当前完整形变矩阵重新验证，应力材料参数由本次实例构造固定。Shape 重建同步销毁缓存。原生着色保证同一四面体的节点不并发更新缓存。</p><h2>两个 BP 的实际游戏视口</h2><p>1920×1080，High，i5-12600KF / RTX 4070 Ti。每阶段预热后 300 帧；两个接触求解器全程运行。移动阶段球体连续往复，而非冻结按压。独立进程存在噪声，不能把本次差值视为硬件无关保证。</p><table><tr><th>阶段</th><th>本轮无缓存对照</th><th>精确缓存</th></tr>'''+dual+'''</table><p>此前两人静态基准约 75 ms，本轮对照约 84 ms，存在运行间波动。缓存后的结果仍约 13 FPS，不能宣称达成大幅帧率提升。释放阶段两路径均回到当前系统的零求解器休眠；这不是完整连续释放软体动力学验证。</p><h2>预分解原型：只验证固定拉伸矩阵</h2>'''+f'<p>真实网格 {m["nodes"]} 个节点、{m["tets"]} 个四面体；自由节点 {m["free_nodes"]}。稀疏 LU 分解 {m["factor_ms"]:.3f} ms，后续三个坐标回代中位数 {m["backsolve_median_ms"]:.3f} ms，分解数据 {m["factor_bytes"]/1024**2:.3f} MiB。线性相对残差 {m["linear_relative_residual"]:.2e}。</p>'+'''<p><strong>这不是完整 PD 接触求解器，也不是 Chaos 提速结果。</strong>尚未计入局部旋转、非线性体积约束、动态接触、支承和表面更新；没有证明与当前形状一致。只是确认本人物规模的预分解在内存和回代成本上值得继续开发。</p>'''+f'<p>可碰撞表面占自由节点 {m["surface_fraction_of_free"]*100:.1f}%，而非局部碰撞论文中的约 5% 有利情形。单标量表面稠密矩阵就需 {m["dense_surface_schur_float64_bytes"]/1024**2:.2f} MiB（尚未含非线性接触）。不优先套用表面消元路线。</p>'+'''<h2>使用与边界</h2><p>缓存尚未证明稳定的端到端收益，默认保持 vam.Contact.MaterialCache=0。2 启用精确缓存，1 只缓存几何。修改后重置接触。最后缓存候选复测稳定受压达到 33.77 ms，未重复出首轮收益，因此不替换生产 DLL。当前没有改为预分解物理模型，没有启用近似应力或已被拒绝的 Newton polar。未进行视觉验收或 Game/Cook 验证。</p><p><a href="results.json">全部结果</a> · <a href="matrix-preflight.json">矩阵实验</a> · <a href="../ContactTwoCharacters/cache-exact/frames.csv">双人物逐帧数据</a> · <a href="../ContactInsights/cache-final/runtime.log">最终回归日志</a></p>'''
(out/'compare.html').write_text(html,encoding='utf-8')
print('Report saved:',out/'compare.html')
