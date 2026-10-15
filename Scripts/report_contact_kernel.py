"""Report independent microbenchmarks without treating their cost as frame time."""
from pathlib import Path
import json, csv, numpy as np
r = Path(__file__).resolve().parents[1]/'Saved/ContactInsights'
k = json.loads((r/'kernel/results.json').read_text())
b = json.loads((r/'kernel/batch-results.json').read_text())
serial = {}
for phase in ['world_moving','world_held']:
    rows = {x['Name']:x for x in csv.DictReader((r/'serial'/(phase+'-GameThread.csv')).open(encoding='utf-8-sig'))}
    n = int(rows['VamBreastContact']['Count'])
    serial[phase] = {key: float(rows[key]['Incl'])*1000/n for key in ['VamBreastContact','VamContactNativeMaterial']}
serial['gpu'] = []
for f in sorted((r/'serial').glob('gpu-*.bin')):
    a=np.fromfile(r/f.name,np.float32);c=np.fromfile(f,np.float32)
    serial['gpu'].append({'file':f.name,'bit_identical':bool(np.array_equal(a,c))})
(r/'kernel/serial-results.json').write_text(json.dumps(serial,indent=2))
rows=''.join(f"<tr><td>{s}</td><td>{v['ns_per_incidence']['0']:.1f}</td><td>{v['stress_ablation_ns']:.1f}</td><td>{v['hessian_ablation_ns']:.1f}</td></tr>" for s,v in k.items())
batch=''.join(f"<tr><td>{s}</td><td>{v['median_12_iterations_ms']:.3f}</td><td>{v['max_endpoint_error_cm']:.9g}</td></tr>" for s,v in b.items())
html=f'''<!doctype html><meta charset="utf-8"><title>Chaos GS 深入分析</title>
<style>body{{background:#141a24;color:#e8edf5;font:17px/1.65 system-ui;max-width:1040px;margin:48px auto;padding:0 24px}}h2,a{{color:#8bbfff}}table{{border-collapse:collapse;width:100%}}td,th{{padding:10px;border-bottom:1px solid #39485c;text-align:left}}strong{{color:#f4d18b}}</style>
<h1>Chaos GS：材料计算与并行分组</h1>
<p>2026-10-08 · 使用当前 C5 的真实四面体拓扑。正式人物的物理参数和运行路径未修改。</p>
<h2>1. 原生材料函数消融基准</h2>
<p>直接调用安装版 Chaos 的 AddHyperelasticResidualAndHessian；通过派生类临时关闭应力或 Hessian 回调。仅用于诊断，不能用关闭这些计算作为优化。</p>
<table><tr><th>冻结形变场</th><th>完整调用 ns</th><th>去应力减少 ns</th><th>去 Hessian 减少 ns</th></tr>{rows}</table>
<p>0 为静止；1/2 为不同程度的合成压缩/剪切。预热后 7 轮、每轮 24 遍，交替消融顺序。差值含替代回调成本，非精确 exclusive timer。</p>
<p><strong>非零形变场中，应力约占单次材料调用的 74%，Hessian 约 5%。</strong>源码确认应力调用 SVD 极分解；本轮未把极分解与应力其余运算分别计时。</p>
<h2>2. 真正连接 GS 构造接口的批大小</h2>
<table><tr><th>每批顶点数</th><th>12 轮材料 GS 中位 ms</th><th>最大终点误差 cm</th></tr>{batch}</table>
<p>独立材料问题，不含弱支承/体积修正/碰撞；每组预热 + 7 次，末尾反向重复 1/5。所有组使用同一颜色顺序与原生材料。不能将本表的加速比直接乘到完整角色帧耗时。</p>
<h2>3. 完整人物关闭并行对照</h2>
<p>球稳定按住：接触组件 {serial['world_held']['VamBreastContact']:.3f} ms；主求解 {serial['world_held']['VamContactNativeMaterial']:.3f} ms。此前默认分别约 35.730 / 21.060 ms。工程测试通过，4 个 GPU 快照逐位一致，但显著变慢，未采用。</p>
<h2>工程决策</h2><ol><li>保留原生并行；测试更细分组的生产接入。当前 Flesh 的 GS owner 未暴露实例 batch 配置，不能靠原有 XPBDBatchSize CVar 生效。</li><li>重点研究应力/极分解的批量 SIMD 或误差受控快速求解；保留奇异/翻转回退。不能简单复用上一轮材料结果。</li><li>完整按压轨迹的形状、体积、穿透、翻转与性能都通过后，才采用候选实现。当前尚未降低正式人物耗时。</li></ol>
<h2>研究依据</h2><ul><li><a href="https://arxiv.org/abs/2306.09021">PBNG：准静态超弹性的逐顶点 GS</a></li><li><a href="https://ankachan.github.io/Projects/VertexBlockDescent/index.html">VBD：逐顶点块下降与并行实现</a></li><li><a href="https://matthias-research.github.io/pages/publications/stablePolarDecomp.pdf">稳健旋转提取方法</a></li><li><a href="https://dev.epicgames.com/documentation/unreal-engine/chaos-flesh-overview">Epic Chaos Flesh 概述</a></li></ul>
<p><a href="timings.csv">消融原始 CSV</a> · <a href="timings.csv.batch.csv">分组原始 CSV</a> · <a href="runtime.log">测试日志</a> · <a href="../serial/capture.utrace">串行对照 Trace</a></p>'''
(r/'kernel/compare.html').write_text(html,encoding='utf-8')
print(r/'kernel/compare.html')
