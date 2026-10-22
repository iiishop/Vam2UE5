"""Attach measured evidence and explicit limitations to the knee repair video report."""
import json,re,shutil,gzip
from PIL import Image
from pathlib import Path
base=Path('Saved/KneeDeepRepair');out=Path('Evidence/KneeDeepRepair');read=lambda p:json.loads(p.read_text())
checks=read(base/'pie-final/validation.json');editor=read(base/'editor-final/validation.json');assert len(checks)==4 and all(x['capture_valid'] for x in checks+editor)
bundle=read(base/'candidate-3/bundle.json');assert bundle['status']=='committed' and bundle['independent_reload_verified']
assert 'Result={Success} Name={SourceOperatorDriver}' in (base/'tests.log').read_text(errors='replace')
for name in ['bundle.json','recipe.json']:shutil.copy2(base/'candidate-3'/name,out/name)
recipe=read(base/'candidate-3/recipe.json');shutil.copy2(Path(recipe['family']),out/'family.json');recipe['family']='family.json';(out/'rebuild-recipe.json').write_text(json.dumps(recipe,indent=2))
for name in ['build','reload','verify']:shutil.copy2(base/'candidate-3'/(name+'.log'),out/('asset-'+name+'.log'))
for name in ['compile-4.log','compile-5.log','tests.log','quaternion-test-failure.log','reconstruction-threshold-failure.log']:shutil.copy2(base/name,out/name)
shutil.copy2(base/'candidate-1/build.log',out/'rejected-continuation.log');shutil.copy2(base/'candidate-2/build.log',out/'rejected-local-smoothing.log')
for name in ['analysis.json','before-after-audit.json']:shutil.copy2(base/'pie'/name,out/name)
shutil.copy2(base/'editor-final/validation.json',out/'editor-validation.json')
shutil.copy2(base/'editor-final/identity.json',out/'editor-identity.json')
for view in ['side','rear','oblique']:Image.open(base/'editor-final/M/M-deep-kneel'/('deep-'+view+'.png')).save(out/('editor-'+view+'.png'))
for group,root in [('C',base/'pie-final/C'),('M',base/'pie-final/M'),('Editor',base/'editor-final/M')]:
 with gzip.open(out/(group+'-runtime.log.gz'),'wb') as f:f.write((root/'runtime.log').read_bytes())
mcheck=next(r for r in read(base/'pie-final/analysis.json')['surfaces'] if r['group']=='M' and r['action']=='deep-kneel' and r['frame']==165)
rows=[]
for r in read(base/'pie-final/before-after-audit.json'):
 if r['action']=='deep-kneel' and r['frame']==165:
  s=r['stages'][-1];fmt=lambda a:' / '.join(f'{v:.2f}°' for v in a);rows.append(f'<tr><td>{r["group"]}</td><td>{fmt(s["tracked_edge_angles_before"])}</td><td>{fmt(s["tracked_edge_angles_after"])}</td><td>{s["max_change_cm"]:.3f} cm</td></tr>')
findings=f'''<section id="findings"><h2>图中膝盖尖折已在 C、M 实拍中修复</h2><p>角度、相机、灯光不变：两组实际达到 155°。真实侧视和斜视中，膝前恢复连续圆弧。保留原腿部与臀部 Jiggle，未靠缩小屈曲、换灯光或关闭修形隐藏问题。最终视觉验收仍由用户查看下方视频。</p><h3>根因与代码</h3><p>最终权重和静态辅助骨层原本就存在深屈曲压缩。旧膝部 Corrective 把这一压缩后的 LBS 表面作为安全方向基准，并限制蒙皮前逆变换位移，使恢复体积所需的源修形被大量压低；旧日志中 135°–150° 局部只保留约 6%–13%。采样还止于 150°，不能完整覆盖 155°。</p><p>修改通用 VamKneeCorrectiveBuilder：以有效源姿态的连续角度增量进行既有三角形/边长保护，异常时回退到上一有效源姿态，而非压扁的 LBS。随后用最终实际蒙皮算子反解并验证重构，采样扩展为 −15° 至 165°、间隔 5°。没有关闭安全检查；右侧源目标局部确有问题，155° 最大保护调整 0.614 cm。失败试验日志一并保留。</p><p>验证：前后主骨矩阵一致；最终权重层和静态辅助骨层表面变化为 0，改进首次出现在 Corrective 层。表中是原先追踪的左右硬折边，数值下降与真实轮廓改善相符；最大位移是修复量，不是相对 VaM 的误差。</p><table><tr><th>方案</th><th>155° 修复前夹角</th><th>修复后夹角</th><th>腿部最大修复位移</th></tr>{''.join(rows)}</table><h3>验证与新角色</h3><p>C++ 编译（含等价四元数符号规范化）、资产 build/reload/verify、4 段 PIE 动作、M 的 Editor 深跪姿、膝部驱动自动测试均已实跑。<a href="before-after-audit.json">逐层数据</a> · <a href="analysis.json">接缝与几何检查</a> · <a href="validation.json">PIE</a> · <a href="editor-validation.json">Editor</a> · <a href="tests.log">自动测试</a> · <a href="asset-verify.log">资产校验</a></p><p>新 BP：<code>{bundle['blueprint']}</code><br>身份：<code>{bundle['identity']}</code>。在内容浏览器加载此 BP 查看；原 BP 未覆盖。生成器已更新，之后正常重建也使用同一修复。</p><details><summary>最终 Editor 实拍侧视图</summary><img src="editor-side.png"></details><p>12 组分层快照中的同源重复点最大差为 0；8 个视频流与 24 个解码抽帧已核验。<a href="media-validation.json">视频校验</a></p><h3>范围</h3><p>这是深屈曲蒙皮修形修复。C 的接触求解仍在高屈曲帧停用，未宣称修复软组织自碰撞；M 在膝后压缩区仍有相对 C 的局部几何标记（155°：{mcheck['new_sharp_edges']} 条新增硬边阈值、{mcheck['opposed_normals_vs_reference']} 个相对反向面），不是本次已消除的膝前尖折；未宣称 M 全表面或完整接触系统通过。这些相对标记不直接等于真实网格翻转。本轮未处理历史 Editor/PIE 动态辅助骨逐帧差异。155° 使用明确标记的诊断膝输入，生产关节限位未改。视频 30 FPS 为编码速度，不是性能测试。保护未证明任意极端体型或超过 165° 的姿态正确。</p><p>参考：<a href="https://dev.epicgames.com/documentation/en-us/unreal-engine/pose-driver-in-unreal-engine">Epic：姿态驱动修形</a>；<a href="https://studios.disneyresearch.com/2016/07/11/real-time-skeletal-skinning-with-optimized-centers-of-rotation__trashed/">Disney：蒙皮关节形变研究</a>。本次实际采用既有源算子与 Morph 路径，没有引入论文中的 CoR 求解器。</p></section>'''
p=out/'report.html';s=p.read_text();s=re.sub(r'<section id="findings">.*?</section>',lambda m:findings,s,flags=re.S).replace('左修复前 / 右候选','左修复前 / 右修复后').replace('上修复前 / 下候选','上修复前 / 下修复后');p.write_text(s,encoding='utf-8')
print(p)
