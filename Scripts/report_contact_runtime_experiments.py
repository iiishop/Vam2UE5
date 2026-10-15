from pathlib import Path
import csv, json, numpy as np
root=Path(__file__).resolve().parents[1]/'Saved/ContactInsights'
results={}
names=['fixed5','fixed1','quiet','metrics','polar','combined','production']
keys=['VamBreastContact','VamContactNativeMaterial','VamContactVolumeConstraints','VamContactLocalCompression','VamContactPostCollision']
for name in names:
 d=root/name
 if not (d/'world_held-GameThread.csv').exists():continue
 result={'phases':{},'gpu':[], 'test':json.loads((d/'summary.json').read_text())}
 for phase in ['press_left','world_moving','world_held','disabled_after']:
  rows={x['Name']:x for x in csv.DictReader((d/(phase+'-GameThread.csv')).open(encoding='utf-8-sig'))}
  ticks=int(rows['VamBreastContact']['Count'])
  result['phases'][phase]={'ticks':ticks,'ms':{k:float(rows[k]['Incl'])*1000/ticks for k in keys if k in rows}}
 for f in sorted(d.glob('gpu-*.bin')):
  a=np.fromfile(root/f.name,np.float32).reshape(-1,3); b=np.fromfile(f,np.float32).reshape(-1,3)
  delta=np.linalg.norm(a.astype(float)-b.astype(float),axis=1)
  result['gpu'].append({'file':f.name,'bit_identical':bool(np.array_equal(a,b)),'max_cm':float(delta.max()),'rms_cm':float(np.sqrt(np.mean(delta**2)))})
 results[name]=result
out=root/'native-adapter';out.mkdir(exist_ok=True)
(out/'results.json').write_text(json.dumps(results,indent=2))
labels={'fixed5':'原生材料 / 批大小 5 对照','fixed1':'原生材料 / 批大小 1','quiet':'原路径 / 关闭高频日志','metrics':'原路径 / 几何限位缓存','polar':'批大小 1 + Newton polar（拒绝采用）','combined':'批大小 1 + 几何限位缓存','production':'最终默认版本复测'}
rows=''
for name,r in results.items():
 m=r['phases']['world_held']['ms'];exact=all(x['bit_identical'] for x in r['gpu']);error=max(x['max_cm'] for x in r['gpu'])
 rows+=f'<tr><td>{labels[name]}</td><td>{m["VamBreastContact"]:.3f}</td><td>{m["VamContactNativeMaterial"]:.3f}</td><td>{"四个快照逐位一致" if exact else f"最大误差 {error*10:.3f} mm"}</td></tr>'
html='<!doctype html><meta charset="utf-8"><title>Chaos Contact · 原生 GS 集成实验</title><style>body{background:#151a24;color:#e6ecf7;font:17px/1.7 system-ui;max-width:1150px;margin:40px auto;padding:0 24px}h2,a{color:#8fc4ff}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}td,th{padding:10px;border-bottom:1px solid #425066;text-align:left}strong{color:#f4ce8b}</style><h1>原生 GS 分组优化与其他候选</h1><p>2026-10-09 · UE 5.8.3 · 相同 C5 人物、真实玻璃球运动轨迹、原有网格/材料/12 次迭代。下表是稳定按住阶段 GameThread 接触组件计时；不是游戏总帧时间，也不是四人物测试。</p><table><tr><th>方案</th><th>接触总计 ms</th><th>GS ms</th><th>GPU 顶点对照</th></tr>'+rows+'</table><h2>采用</h2><p><strong>仅默认采用批大小 1。</strong>重用 UE 的 GS、材料、着色顺序和支承约束，只改变同色节点任务分组；不减少网格、迭代、按压或 Jiggle 幅度。构造材料时使用真正初始 rest 坐标，逐步执行原生弱支承初始化。遇到不支持的布局保留原路径。</p><h2>未采用</h2><ul><li>Newton polar：有速度收益，但出现约 7.9 mm 网格偏差，工程测试通过不代表形状等价。</li><li>几何限位缓存：局部约节省 0.2 ms，但完整组件未证明额外收益，默认关闭。</li><li>日志抑制：没有明确收益，不修改全局日志配置。</li></ul><h2>范围与复现</h2><p>每个独立进程一次完整轨迹；时序存在进程间噪声，没有宣称置信区间。四个 GPU 快照一致不等于证明所有姿态、所有帧都一致。既有工程检查包含穿透、体积、单元翻转、表面拉伸、重置和开关。未进行视觉验收、Game 构建或 Cook。</p><p>控制台 <code>vam.Contact.NativeGSBatch 0</code> 后重置接触可回到原引擎路径；1 为当前默认，5 为原批大小适配器对照。实验 <code>vam.Contact.ExperimentalPolar</code> 与 <code>vam.Contact.RestMetrics</code> 均保持 0。</p><p><a href="results.json">全部阶段与 GPU 误差 JSON</a> · <a href="../production/capture.utrace">最终版本 Insights Trace</a> · <a href="../production/runtime.log">最终回归日志</a></p>'
(out/'compare.html').write_text(html,encoding='utf-8')
for name,r in results.items():print(name,r['phases']['world_held'],r['gpu'])
