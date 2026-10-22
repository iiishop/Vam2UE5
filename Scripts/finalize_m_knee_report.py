"""Attach actual acceptance results; refuse to publish a fabricated success."""
import json,gzip,shutil,re,html
from pathlib import Path
R=Path('Saved/MKneeOperator');O=Path('Evidence/MKneeOperator')
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
pie=read(R/'pie-accepted/operator-analysis.json');editor=read(R/'editor-accepted/operator-analysis.json')
assert pie['gpu_matches_frozen_cpu'] and editor['gpu_matches_frozen_cpu']
for folder in ['pie-accepted/M','editor-accepted/M','contact-10','contact-regression']:
    assert 'Result={Success} Name={ContactRuntime}' in (R/folder/'runtime.log').read_text(errors='replace')
assert 'Result={Success} Name={SourceOperatorDriver}' in (R/'knee-unit.log').read_text(errors='replace')
assert 'OK' in (R/'algebra-tests.log').read_text(errors='replace')
active=read(R/'contact-10/M-contact-10/state.json');blocked=read(R/'contact-regression/M-contact-90/state.json')
assert active['contact_solver_active'] and not blocked['contact_solver_active']
agreement=read(R/'editor-pie-agreement.json');baseline=read(R/'baseline-editor-pie-jiggle.json')
for x,y in zip(agreement,baseline):
    assert x['stages']['03-corrective/surface.json']['max_cm']==0
    assert x['stages']['04a-operator-corrected/postskin-correction.json']['max_cm']==0
    assert x['stages']['04a-operator-corrected/surface.json']['max_cm']==y['max_cm']
frames=[f for x in [pie,editor] for a in x['actions'] for f in a['frames']]
assert all(f['new_triangle_orientation_reversals']==0 and f['logical_duplicate_max_gap_cm']==0 and f['gpu_normals_finite'] for f in frames)
bundle=read(R/'candidate-2/bundle.json')
summary=dict(operator_repair='PASS',asset_identity=bundle['identity'],configuration=bundle['configuration'],blueprint=bundle['blueprint'],pie='PASS',editor='PASS',source_operator_unit='PASS',algebra_tests=4,gpu_max_difference_cm=max(f['gpu_vs_cpu_cm']['max'] for f in frames),new_triangle_orientation_reversals=0,logical_duplicate_max_gap_cm=0,contact_10='active GPU; smoke regression PASS',contact_90='BLOCKED by existing rest-volume protection',editor_pie_static_and_correction_exact=True,editor_pie_jiggle_difference_unchanged_from_baseline=True,visual_scope='Visible improvement; residual local shape differences documented, final user visual acceptance pending',implementation_commits=['a341556','91934ff'])
(O/'acceptance.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
for src,name in [(R/'editor-accepted/operator-analysis.json','editor-analysis.json'),(R/'editor-accepted/identity.json','editor-identity.json'),(R/'editor-accepted/validation.json','editor-validation.json'),(R/'candidate-2/bundle.json','asset-bundle.json'),(R/'asset-code-consistency.json','asset-code-consistency.json'),(R/'editor-pie-agreement.json','editor-pie-agreement.json'),(R/'baseline-editor-pie-jiggle.json','baseline-editor-pie-jiggle.json'),(R/'contact-10/M-contact-10/state.json','contact-10-state.json'),(R/'contact-regression/M-contact-90/state.json','contact-90-state.json')]:shutil.copy2(src,O/name)
logs=['compile-1.log','compile-2.log','compile-3.log','compile-harness.log','compile-audit.log','algebra-tests.log','knee-unit.log','candidate-2/build.log','candidate-2/reload.log','candidate-2/verify.log','pie-final/M/runtime.log','pie-verified/M/runtime.log','pie-accepted/M/runtime.log','editor-accepted/M/runtime.log','contact-10/runtime.log','contact-regression/runtime.log']
(O/'logs').mkdir(exist_ok=True)
for f in logs:
    with gzip.open(O/'logs'/(f.replace('/','-')+'.gz'),'wb') as z:z.write((R/f).read_bytes())
for view in ['side','rear','oblique']:shutil.copy2(R/f'editor-accepted/M/M-deep-kneel/deep-{view}.png',O/f'editor-{view}.png')
peak=pie['actions'][0]['frames'][-1];table=[]
for region,label in [('0_knee_anterior','左膝前'),('0_knee_posterior','左膝后'),('1_knee_anterior','右膝前'),('1_knee_posterior','右膝后')]:
    values={x['state']:x for x in peak['regions'] if x['region']==region and x['target']=='smoothed'}
    table.append(f'<tr><td>{label}</td><td>{values["before"]["rms"]:.4f}</td><td>{values["after"]["rms"]:.4f}</td><td>{values["after"]["p95"]:.4f}</td><td>{values["after"]["max"]:.4f}</td></tr>')
stats=pie['actions'][0]['cpu_us']
text=f'''<p id="validation-summary"><b>M 与 Knee corrective 的算子一致性修复：PASS。</b>真实普通 BP 的 PIE 和 Editor 重放通过，最终视觉验收仍由用户判断。</p>
<ul><li>C++ 构建通过；4 项方向传输代数测试通过；现有 SourceOperatorDriver 自动测试通过。</li><li>新角色独立构建、重新加载、发布校验通过。身份：<code>{bundle['identity']}</code>。</li><li>PIE：双腿 270 帧、单腿 180 帧；Editor：双腿 270 帧。视频为固定 60 Hz 输入、30 fps 输出，不能当作真实游戏帧率测试。</li><li>九个冻结帧的 GPU 与 CPU 最大差 {summary['gpu_max_difference_cm']:.6f} cm（核对门槛 0.02 cm）。未新增三角形朝向反转；逻辑重复点位置差 0；法线均为有限值。</li><li>单次补偿更新 CPU 中位数 {stats['median']/1000:.3f} ms，P95 {stats['p95']/1000:.3f} ms。测试机器有明显内存压力；这不包含整个 GPU 图，也不是每帧角色总开销。</li></ul>
<h3>155° 双腿姿态，相对同矩阵 VaM smoothed 表面</h3><table><tr><th>区域</th><th>原 M RMS / cm</th><th>本次 RMS / cm</th><th>本次 P95 / cm</th><th>本次 Max / cm</th></tr>{''.join(table)}</table>
<p>使用保留的 source ID 映射；相机一致、实际主骨矩阵一致、Bind/权重/Pre-skin 输入一致。臀腿区域另有已记录的源隐藏 Morph 输入差异，不混入本次膝部结论。</p>
<h3>尚存差异与明确边界</h3><ul><li>三角形对 [50,51] 仍约 72°，原 M 约 84°；同点 VaM raw 约 52°、smoothed 约 29°。本次去除了修形方向的二次拉伸，但保留了 M 基础肌群形变；残余基础形状与 VaM 平滑差异没有被宣称消失。</li><li>Editor/PIE 静态辅助骨、corrective 和本次补偿逐点一致。Jiggle 后的最大差在三个时刻为 0.640 / 0.352 / 0.111 cm；与修复前基线完全相同，未新增该差异，也未声称两种 World 的动态状态已逐帧一致。</li><li>Contact 10°：真实 GPU 求解运行，8822 粒子，诊断无四面体翻转。Contact 90°：仍被已有 rest-volume 保护停用，不能标为深屈膝碰撞通过；未放宽安全检查。</li><li>两次审计等待失败日志保留。修复诊断时序后重新完成完整运行，不将失败轮次视频当作验收输出。</li></ul>
<h3>使用新资产</h3><pre>{html.escape(bundle['configuration'])}\n{html.escape(bundle['blueprint'])}</pre><p>请使用该新身份 BP；旧 baked mesh / Deformer 不代表本次修复。资产构建后仅变更了两个诊断文件，生产形变、Bind、权重、Morph 和 Shader 的 186 项指纹核对记录见 <a href="asset-code-consistency.json">身份说明</a>；验证运行有独立身份。</p>
<p>代码已推送：<code>a341556</code>（修形传输）、<code>91934ff</code>（审计同步）。<a href="acceptance.json">验收摘要</a> · <a href="editor-analysis.json">Editor 数值</a> · <a href="editor-pie-agreement.json">World 分层对照</a> · <a href="contact-10-state.json">Contact 10°</a> · <a href="contact-90-state.json">Contact 90° 限制</a> · <a href="animation-research.html">《绝区零》研究图解</a> · <a href="../KneeVaMComparison/report.html">原 VaM 同动作审计</a></p>
<details><summary>Editor 真实截图</summary><img src="editor-side.png"><img src="editor-rear.png"><img src="editor-oblique.png"></details>'''
p=O/'report.html';s=p.read_text(encoding='utf-8');assert '<p id="validation-summary">详细验收待最终数据核对。</p>' in s;s=s.replace('<p id="validation-summary">详细验收待最终数据核对。</p>',text);p.write_text(s,encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False,indent=2))
