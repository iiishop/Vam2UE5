"""Attach verified execution evidence and bounded conclusions to the comparison."""
import json,shutil,subprocess
from pathlib import Path
root=Path('Saved/LegChainVariants');out=Path('Evidence/LegChainVariants');read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
editor=read(root/'editor-1/validation.json');pie=read(root/'pie-1/validation.json');assert len(editor)==len(pie)==6 and all(x['valid'] for x in editor+pie)
assert all('Result: Succeeded' in (root/f).read_text(errors='replace') for f in ['compile.log','compile-tests.log'])
unit=(root/'unit.log').read_text(errors='replace')
for name in ['EquivalentEulerLimits','LongitudinalChain','PreserveSupportSlots']:assert 'Result={Success} Name={'+name+'}' in unit
checks=out/'checks';checks.mkdir(exist_ok=True)
for f in ['compile.log','compile-tests.log','unit.log','installed-binaries.json']:shutil.copy2(root/f,checks/f)
shutil.copy2(root/'editor-1/identity.json',checks/'editor-identity.json');shutil.copy2(root/'editor-1/validation.json',checks/'editor-validation.json')
shutil.copy2(root/'editor-1/all/runtime.log',checks/'editor-runtime.log')
for g in ['R','L','T','V','S','C']:
 for f in ['runtime.log','command.json','process.json']:shutil.copy2(root/'pie-1'/g/f,checks/(g+'-'+f))
 folder=checks/('Editor-'+g);folder.mkdir(exist_ok=True);src=root/'editor-1/all'/(g+'-seated-stop');shutil.copy2(src/'state.json',folder/'state.json')
 from PIL import Image
 Image.open(src/'oblique-material.png').save(folder/'oblique-material.png',optimize=True)
checks_data=dict(compile='PASS',ue_leg_tests='3 PASS',PIE_recordings='18 complete; C contact safety rejection separately recorded',editor='6 modes replayed',asset='Existing 61a28e74 asset unchanged; runtime operator validation only',source_commit=subprocess.check_output(['git','rev-parse','HEAD'],text=True).strip(),contact='NOT FIXED',production_promotion=False)
(out/'checks.json').write_text(json.dumps(checks_data,indent=2))
summary='''<section style="background:white;padding:20px;border-radius:12px"><h2>本轮判断</h2><p><b>三个结构候选已实跑；目前 S（独立站点）最值得保留为候选，但还不能证明它比你认可的 C 更好。</b>原有生产默认没有自动切换到候选。</p><ul><li>L 旧链条：区域 Jiggle 骨与新增站点同时跟随整段曲线，坐姿最大静态位移约 1.93 cm。</li><li>T 仅平移：去掉曲线框架旋转和缩放，最大静态位移约 1.61 cm；屈伸时部分轮廓更接近参照，但仍改变整段腿的形态。</li><li>V 局部体积：单骨配对伸缩，最大静态位移约 1.95 cm；本轮没有看到足以支持采用的明显优势。</li><li>S 独立站点：原有 Jiggle 骨不再被曲线整体带动，最大静态位移约 0.46 cm；更接近原来认可的轮廓，仍保留少量站点联动。</li></ul><p>这些数字是 A1 的骨链层位移，不是真实人体误差，也不是越小越好。观察支持“全区域跟随同一曲线会过度改变轮廓”，但还不能将所有缺陷归到某一个数值。三个候选是同一绑定上的不同驱动算子；没有冒充三套完成解剖标定的肌肉 Rig。</p><p>研究得到的下一步方向：为前后肌群定义独立起止附着点，按各自路径长度控制肌腹，而保留 C 的正常关节与 Jiggle 支撑。这个方向需要新的权重/附着数据与重新烘焙验证，本轮没有偷偷用旧 Corrective 证书替代。</p><p>验证：UE 编译成功；3 项腿部测试通过；PIE 18 段录像完成；Editor 6 模式重放完成。实际主骨矩阵、根移动与相机一致性见 analysis.json。C 的历史接触停算仍存在，不能标为接触 PASS。<a href="checks.json">验证范围</a> · <a href="checks/editor-runtime.log">Editor 日志</a> · <a href="checks/unit.log">UE 测试日志</a></p></section>'''
p=out/'report.html';s=p.read_text(encoding='utf-8');pos=s.index('<h2>屈伸</h2>');s=s[:pos]+summary+s[pos:];p.write_text(s,encoding='utf-8')
print('Final evidence validated',out/'report.html')
