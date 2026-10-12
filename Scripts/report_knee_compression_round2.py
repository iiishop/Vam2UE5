"""Append-only second-round report. Keep failed candidates and label inactive contact captures."""
import json,re,subprocess,sys,html
from pathlib import Path
root=Path(__file__).resolve().parents[1];r=root/'Saved/LowerBodyContact/KneeCompression';out=r/'round-2';rows=[]
for name in ['candidate-4','candidate-5','candidate-6']:
 for p in sorted((r/name/'pie-soft').glob('*/state.json')):
  s=json.loads(p.read_text());text=s['contact_status'];m=re.search(r'max contact residual ([0-9.]+) cm',text);rows.append(dict(candidate=name,pose=p.parent.name,active=s['contact_solver_active'],acceptance=s['acceptance'],max_residual_cm=float(m[1]) if m else None,status=text,audit=s['audit_bundle']))
(out/'capture-summary.json').write_text(json.dumps(rows,indent=2,ensure_ascii=False),encoding='utf-8')
log=(out/'candidate-6-tests/final-tests.log').read_text(errors='replace');tests=re.findall(r'Test Completed\. Result=\{(\w+)\} Name=\{([^}]+)\} Path=\{([^}]+)\}',log);(out/'test-results.json').write_text(json.dumps(tests,indent=2))
table='\n'.join('| '+v['candidate']+' | '+v['pose']+' | '+str(v['active'])+' | '+str(v['max_residual_cm'])+' |' for v in rows)
text='''---
title: 屈膝软组织接触 · 第二轮实测
subtitle: 目标网格翻转与接触邻接分开诊断；未通过的姿态明确保留
lang: zh
---
## A 实测状态 {span=2}
```callout warn
深屈膝脂肪挤压仍未验收。接触启动前，动画驱动的体积目标已有翻转。截图中未启动求解器的姿态不能作为成功的软体接触效果。主工程正式 DLL 未替换。
```
本轮修改真实 Builder / GPU 接触 / A1 导出路径，生成独立身份的正常 BP；没有修改 Glute/Leg Jiggle 参数，没有取消翻转检查。

## B 翻转最早发生在哪一层
旧候选 4 的规范四面体均为正体积。约 118.6° 屈膝，在仅应用实际骨骼矩阵、不加姿态 Morph 时就有左 236 / 右 262 个无效单元；加入实际 Morph 后降至 186 / 213。399 个翻转单元中 380 个位于规范空间高度 40–60 cm 的膝部带。
坐姿仅骨骼驱动阶段已大量翻转。最终目标有 1738 个，其中 1608 个位于 80–100 cm 的髋根高度带。不是 GPU 接触首次造成的翻转。
分层 bones-only 数据没有声称包含 Morph；运行时 shaped-reference 才包含真实 Morph。完整逐单元记录包含单元 ID、四个粒子、区域、硬绑定数量、规范中心和各层有符号体积。

## C 尝试与取舍
- 候选 5 尝试踝部向脚骨过渡：60° 姿态无效单元由 1 个增加到 5 个，已撤回；保留完整失败证据。
- 接触查询增加同一连续区域的直接邻接及同向表面过滤。
- 候选 6 把生成时的原生输入三角形编号保存到粒子并上传 GPU。即使左右体积重新网格化成了不同粒子，也能排除共享源三角形顶点的伪接触。不按空间距离焊接皮肤。
- 两个不同角色的源编号可相同，因此来源排除限定在同一实例；旧资产缺少来源数据时保留原有兼容行为。
- 独立离线局部/全局正 Jacobian 投影实验：固定内部支撑，100 次后仍有 96 个翻转，最大改动约 3.78 cm；完全放软内部支撑，300 次后仍有 1 个翻转且内部支撑移动约 1.14 cm。未接入生产，不将离线结果冒充真实 UE 修复。

## D PIE 实际接触状态 {span=2}
| 资产 | 姿态 | 求解器激活 | 最大接触残差 cm |
|---|---|---|---|
'''+table+'''

残差是求解输出相对动画目标的距离，不是接触穿透量，也不等于成功挤压。

## E 构建与回归
C++ 编译成功。候选资产均使用独立 recipe/identity，完成后由独立 UE 进程重载验证。
自动测试已执行：'''+str(len(tests))+''' 项，成功 '''+str(sum(t[0]=='Success' for t in tests))+''' 项。包含同体积不同碰撞组的真实互碰、共享来源排除、跨角色反作用、刚体形状、B1、臀腿与胸部回归。
这批测试使用最终候选 6 的配置和编译代码。正常 BP 的 Editor / PIE 各四姿态已完成；Editor 截图不冒充 PIE 物理解算。四姿态、五层原生表面与候选 4 最大位置差均为 0 cm，骨骼矩阵相同。
候选 6 身份：`489612797e50733ba5dce8ee91e588374416a42ff5c190e9c0a06bba7f80b0e6`。
站立最大残差仍为约 0.6524 cm，未解决；深屈膝仍有 399 个翻转单元，未解决。启动日志中的 Condition failed 信息保留；测试结论取自实际命名 Test Completed 记录。

## F 结构方向与限制 {span=2}
本轮数据支持继续区分“动画吸附目标”和“有效的材料参考状态”。现在下半身把已经折叠的动画体积直接当作材料参考，导致求解器启动前即失败。内部硬绑定的几何定义也需继续修复；仅提高迭代或软化材料不能解决不相容的参考体积。
[WALL·E FEM 作者的实现回顾](https://www.tinymonkey.org/projects/pixar/walle/fem/fem/) 描述了软吸附约束、表面到体积的 Laplace 传递和逐单元参考状态重定向，并限制参考形变梯度以避免退化。它支持继续研究参考状态构造；不意味着当前项目已经实现该方法。
下一项验收仍是：在有效体积状态上出现真实的大小腿相向压缩和侧向摊开，同时保留原有 Jiggle。当前尚未达到。
'''
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render','-','--output',str(out/'report.html'),'--no-open'],input=text,text=True,encoding='utf-8',check=True)
gallery='<section style="padding:24px"><h2>真实 UE 输出（失败姿态仅供定位）</h2><div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:16px">'
for v in rows:
 if v['candidate'] not in ['candidate-4','candidate-6'] or v['pose'] not in ['standing-soft','knee-120-soft']:continue
 im=r/v['candidate']/'pie-soft'/v['pose']/'side-gray.png'
 if im.exists():gallery+=f'<figure><img style="width:100%" src="../{im.relative_to(r).as_posix()}"><figcaption>{html.escape(v["candidate"]+" / "+v["pose"])} · solver={v["active"]}</figcaption></figure>'
gallery+='</div><p><a href="capture-summary.json">逐姿态状态</a> · <a href="test-results.json">实际自动测试结果</a> · <a href="baseline-cells/summary.json">基线逐层计数</a></p></section>'
f=out/'report.html';f.write_text(f.read_text(encoding='utf-8').replace('</body>',gallery+'</body>'),encoding='utf-8')
