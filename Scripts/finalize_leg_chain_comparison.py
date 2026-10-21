"""Package successful chain tests and explicitly failed contact integration trials."""
import argparse,gzip,hashlib,html,json,shutil,subprocess,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('base',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
repo=Path(__file__).resolve().parents[1];run=a.base/'pie-3';bundle=a.base/'candidate-5/bundle.json'
assert not a.out.exists(),'Historical evidence is immutable; choose a new destination'
editor=read(a.base/'editor-2/validation.json');assert len(editor)==3 and all(x['valid'] for x in editor)
for name in ['compile-04-full.log','compile-05-capture.log']:assert 'Result: Succeeded' in (a.base/name).read_text(errors='replace')
unit=(a.base/'unit-and-legacy.log').read_text(errors='replace')
for name in ['FinalCorrective','EquivalentEulerLimits','LongitudinalChain','PreserveSupportSlots']:assert 'Result={Success} Name={'+name+'}' in unit
subprocess.run([sys.executable,str(repo/'Scripts/audit_leg_chain_comparison.py'),str(run)],check=True)
subprocess.run([sys.executable,str(repo/'Scripts/report_leg_chain_comparison.py'),str(run),str(bundle),str(a.out)],check=True)
evidence=a.out/'checks';evidence.mkdir()
for name in ['compile-04-full.log','compile-05-capture.log','unit-and-legacy.log','recipe-tests.log','installed-binaries.json']:
 shutil.copy2(a.base/name,evidence/name)
shutil.copy2(a.base/'editor-2/validation.json',evidence/'editor-validation.json')
shutil.copy2(a.base/'legacy-contact/summary.json',evidence/'legacy-contact-summary.json')
shutil.copy2(a.base/'candidate-5/recipe.json',evidence/'build-recipe.json')
shutil.copy2(a.base/'candidate-5/family.json',evidence/'build-family.json')
for phase in ['build','reload','verify']:shutil.copy2(a.base/'candidate-5'/(phase+'.log'),evidence/('asset-'+phase+'.log'))
rows=[]
for group in 'ABC':
 for world,directory in [('PIE',run),('Editor',a.base/'editor-2')]:
  folder=evidence/(world+'-'+group);folder.mkdir()
  for name in ['runtime.log','command.json','asset-identity.json','process.json','suite.json']:
   shutil.copy2(directory/group/name,folder/name)
 for action in ['flexion','step','seated-stop']:
  src=run/group/(group+'-'+action);samples=read(src/'video.json')['samples']
  stopped=next((s for s in samples if not s['contact_solver_active']),None)
  rows.append(dict(group=group,action=action,frames=len(samples),active_frames=sum(s['contact_solver_active'] for s in samples),first_inactive_frame=stopped['frame'] if stopped else None,first_inactive_status=stopped['contact_status'] if stopped else None))
  shutil.copy2(src/'video/frame-0045.png',a.out/(group+'-'+action)/'frame-45.png')
b=read(bundle);current={k:hashlib.sha256((repo/k).read_bytes()).hexdigest() for k in b['algorithms']}
assert all(r['first_inactive_frame']==31 for r in rows if r['action']=='flexion' and r['group'] in 'AC'),'Update the written interpretation for the observed stop frame'
changed={k:dict(asset_build=v,current=current[k]) for k,v in b['algorithms'].items() if v!=current[k]}
assert {k.replace('\\','/') for k in changed}<= {'Source/VamResourceBrowser/Private/Tests/VamPoseSuite.h'},'Unexpected post-build production implementation change'
manifest=dict(asset_identity=b['identity'],asset_configuration=b['configuration'],post_build_source_changes=changed,post_build_change_scope='Capture-only subject/seat whitelist and disable unrelated PIE pawn collision; production deformation data and code unchanged.',installed_binaries=read(a.base/'installed-binaries.json'),source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=repo,text=True).strip(),editor=editor,contact_frames=rows,chain_status='IMPLEMENTED_AND_REPLAYED',combined_contact_status='FAIL',visual_acceptance='USER_REVIEW_REQUIRED')
manifest['validation_identity']=hashlib.sha256(json.dumps(manifest,sort_keys=True,separators=(',',':')).encode()).hexdigest()
(a.out/'checks.json').write_text(json.dumps(manifest,indent=2),encoding='utf-8')
table=''.join(f'<tr><td>{r["group"]}</td><td>{r["action"]}</td><td>{r["active_frames"]}/{r["frames"]}</td><td>{r["first_inactive_frame"] if r["group"]!="B" else "按设计关闭"}</td></tr>' for r in rows)
summary='''<section style="border-left:6px solid #bf4b36;margin:20px 0"><h2>实测结论：骨链已接入；接触全程联动未通过</h2><p><b>B（Jiggle＋骨链）完整跑完屈伸、交替迈步和急停。</b>A/C 的接触在动作中被安全检查停掉，不能把停算后的画面当作 Chaos 仍在工作的效果。视频左上角已按实际帧标注 ACTIVE / STOPPED。</p><p>旧的 LegResearch 资产也在屈伸第 31 帧（约 1.03 秒）触发同一拒绝；本次 A/C 的结果与之相同。这证明问题在不加新骨链时也存在，不能证明具体的四面体绑定根因已修复。安全检查保留。</p><p>视觉上，骨链改变了屈膝时大腿的整体曲率与膝周过渡；当前观察未见新的明显环状断裂。更好的肌肉真实性仍需你结合视频判断，不宣称解剖模拟或全部姿态已通过。</p><p>正常 BP、新资产生成、独立重载、最终权重 Corrective 校验、Editor 三组检查已完成。PIE 三组均实际运行并记录，A/C 的接触联动判为 FAIL。</p></section><h2>接触真实运行帧数</h2><table><tr><th>组</th><th>动作</th><th>接触有效帧</th><th>首次未运行帧（从 0 开始）</th></tr>'''+table+'''</table><p><a href="checks/legacy-contact-summary.json">旧资产复现</a> · <a href="checks.json">完整检查与独立验证身份</a> · <a href="checks/editor-validation.json">Editor 检查</a></p><details><summary>实现结构与实验边界</summary><ul><li>双侧大小腿各增加 5 个纵向站，共 20 个新变形骨。关节端点固定，邻接骨旋转驱动 Hermite 曲线；区域 Jiggle 节点随曲线坐标系运动。</li><li>既有臀部、腿部 Jiggle、Corrective 保留。两相邻站分享剩余主骨权重，经过既有来源顶点 Weld 与最终 influence 预算；不是复制左腿覆盖右腿，也不是把主骨改成软骨。</li><li>截面补偿是工程近似，不代表精确体积保持。最终权重预算最大丢弃候选质量约 0.998%，三组使用同一新资产；C 不是旧网格的逐位复刻。</li><li>头发使用已还原的静态资源，三组一致。当前比较不包含头发物理开销。</li><li>屈伸和交替迈步是受控姿态测试；急停是根节点平移激励，不冒充完整移动角色控制器。Editor 检查覆盖资产/静态骨链，动态 Jiggle 由 PIE 证明。</li><li>每段 210 帧、7 秒，30 FPS 仅为编码速度，不能当作游戏实测帧率。</li></ul><p>参考思路：<a href="https://docs.blender.org/manual/en/4.0/animation/armatures/bones/properties/bendy_bones.html">Blender Bendy Bones</a>、<a href="https://dev.epicgames.com/documentation/en-us/unreal-engine/control-rig-spline-rigging-in-unreal-engine">UE Spline Rigging</a>。本实现是原生 C++ 曲线辅助骨，不声称完全复刻视频作者隐藏的绑骨逻辑。</p></details><details><summary>接触限制定位</summary><p>初始化拒绝位于 VamBreastContactComponent::Initialize 的当前姿态 rest-volume 校验；动态拒绝位于 GPU 异步诊断后调用 MeasureVolume。MeasureVolume 遇到非正/过小的参考四面体体积等条件会提前返回。日志中后续区域 volume=0 可能只是尚未累计，不能据此宣称整个右腿体积变为零。后续应导出第一处失败四面体的 animated target、绑定和邻域，修复接触目标兼容性；本轮不靠放松安全门槛让三组假通过。</p></details>'''
page=(a.out/'report.html').read_text(encoding='utf-8');page=page.replace('<h1>腿部骨链：三组同条件对比</h1>','<h1>腿部骨链：三组同条件对比</h1>'+summary)
stills='<h2>同一运动帧的轮廓对照</h2><p>屈伸第 45 帧，此时 A/C 接触已停算，仅适合看骨链开启与关闭的差别。</p><div class="grid">'+''.join(f'<section><h3>{g}</h3><img src="{g}-flexion/frame-45.png"></section>' for g in 'ABC')+'</div>'
page=page.replace('<h2>A1 分层数据</h2>',stills+'<h2>A1 分层数据</h2>')
page=page.replace('<h2>资产</h2>','<h2>切换与使用</h2><p>新资产仍是正常 BP_VamCharacter。身体组件上的 <code>Leg Chain Enabled</code> 可在 Editor 或 Blueprint 中切换；现有 Leg/Glute Jiggle 和 Contact 开关独立保留。旧 profile 没有骨链数据，不会凭空改变形状。</p><p>本实验 Jiggle 指臀部＋大小腿，胸部 Jiggle 关闭。为固定三组主骨运动，接触向主骨的力反馈关闭；本轮比较表面形变，不验证全身受力反应。B 组虽然能看到座面，但关闭接触后不提供座面防穿透。</p><h2>资产</h2>')
page=page.replace('最终权重 Corrective 校验','最终权重与 Corrective 资产一致性校验')
(a.out/'report.html').write_text(page,encoding='utf-8')
print(a.out/'report.html')
