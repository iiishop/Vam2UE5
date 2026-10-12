"""Render an honest knee-contact experiment report from persisted UE evidence."""
import json,html,subprocess,sys,re
from pathlib import Path
root=Path(__file__).resolve().parents[1];r=root/'Saved/LowerBodyContact/KneeCompression'
rows=[]
for name,label in [('cage-isolation.json','原基线'),('candidate-1/cage-isolation.json','近端骨支撑'),('candidate-2/cage-isolation.json','连续关节支撑'),('candidate-3/cage-isolation.json','同步姿态修形')]:
 if not (r/name).exists():continue
 for s in json.loads((r/name).read_text()):
  if not s['pose'].startswith('knee-60'):continue
  vals=[s['regions'][str(v)] for v in [2,3]];key='runtime_initial_invalid' if label=='同步姿态修形' else 'posed_invalid';rows.append(f"| {label} | {vals[0][key]} | {vals[1][key]} |")
checks=json.loads((r/'regression-results.json').read_text());limit=(r/'limit-tests.log').read_text(errors='replace');passed='Result={Success} Name={EquivalentEulerLimits}' in limit
bundles=[]
for p in sorted(r.glob('candidate-*/bundle.json')):
 b=json.loads(p.read_text());bundles.append(f"- {p.parent.name}: `{b['identity']}` · {b['status']}")
text='''---
title: 屈膝软组织挤压 · 结构诊断与候选验证
subtitle: 保留历史证据；不把求解器未启动的截图称为接触成功
lang: zh
---
## A 当前结论 {span=2}
```callout warn
接触效果尚未通过。已修复多项支撑/分组/姿态输入错误，但大角度接触网格仍有翻转，安全检查继续拒绝启动。当前候选未安装为主工程的正式修复版。
```
已经确认：原路径不是完整的肌肉动态 FEM；它围绕动画目标求接触残差。不能把仅改变屈膝轮廓等同于发生了脂肪挤压，也不能把 GPU 测试通过等同于真实角色视觉验收通过。

## B 已定位的结构问题
- 同侧臀、大腿、小腿共用体积标签，该标签同时排除了同侧互碰。现在将碰撞标签与体积区域分开，保留原体积约束。
- 内部支撑沿大腿逐渐绑定到小腿骨，屈膝时内部被拉走。改为近端骨支撑，并在关节附近连续过渡。
- 表面已经应用膝部/臀部修形，接触网格却只复制体型 Morph。现在同步活动姿态 Morph，且每帧从规范空间重建下半身目标，避免重复蒙皮已变换的点。
- 接触自动唤醒只检查胸部。现在增加下半身分组的骨骼包围盒。
- 体积封闭用的内表面不是皮肤。现在根据支撑深度筛选暴露边界参与软体表面接触；这是工程近似，仍需继续核查接触误报。

## C 数值证据：60° 请求 {span=2}
| 阶段 | 左侧非正体积单元 | 右侧非正体积单元 |
|---|---:|---:|
'''+ '\n'.join(rows)+'''

前两轮使用 A1 实际骨骼矩阵重建目标；姿态同步轮使用运行时导出的初始化网格，因此含实际活动 Morph。不是 GPU 成功形变的计数。最后一个失败单元位于踝部；大角度和坐姿仍未通过。简单内部权重调和扩散实验将 16 个失败单元增加到 23 个，未采用。

## D 关节表示错误
生产限位数值没有扩大。四元数转为欧拉角时，超过 90° 的 Pitch 可折回，并伴随 Yaw/Roll 增加 180°。旧逻辑逐分量截断这一等价表示，导致部分合法屈膝被拒绝。新逻辑比较两种等价欧拉表示，再按同一限位截断。

等价表示回归：'''+('PASS' if passed else '未确认')+'''. 新资产的实际角度仍以各次 `state.json` 骨骼矩阵为准，历史 Knee120（约92°）截图不能直接冒充新的同姿态对照。

## E 回归与限制
- 候选 3 的 B1、臀腿、胸部及 GPU 碰撞联合回归：'''+str(sum(v[0]=='Success' for v in checks))+''' / '''+str(len(checks))+''' 通过。
- GPU 测试覆盖同一体积区域内独立碰撞分组，双方形变及净外力/力矩抵消。
- 候选 1 接触关闭：四个姿态 × 五层表面，与上一版最大顶点差为 0 cm。
- 已运行正常生成 BP 的 Editor 与 PIE。接触未启动的姿态标为失败，不能作为挤压验收。
- 站立纯软体候选仍出现约 0.68 cm 最大残差，需要排查关节附近/分组边界的接触误报；不能当作想要的组织压缩。
- 未改变 Jiggle 开关/强度/材料参数，也未取消翻转安全检查。当前主工程 DLL 保留上一版。

## F 下一项必须解决的结构问题
使大角度下的接触体积网格保持有效，区分关节形态修形与真实相向接触。接触对必须来自真实皮肤相向表面，不能来自相邻连续表面或内部封口。之后才能验证双方压扁、向侧方摊开、体积误差及 Jiggle 叠加效果。

## G 研究依据与生成身份 {span=2}
[Zelle 等，2009，Thigh-calf contact](https://pubmed.ncbi.nlm.nih.gov/19200996/) 使用有限元/力学模型讨论深屈膝时载荷通过大小腿软组织接触界面传递；接触作用与腿围有关。它支持建模真实接触界面，不提供可直接照搬的本角色参数。

[Waterloo：六种高屈膝姿态的接触参数](https://uwspace.uwaterloo.ca/items/464bd4eb-bbb8-4c91-a0a3-741241f50f40) 区分接触起点、面积、力及姿态。不能仅凭一个固定角度就强制制造挤压。

'''+ '\n'.join(bundles)
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render','-','--output',str(r/'report.html'),'--no-open'],input=text,text=True,encoding='utf-8',check=True)
gallery='<section style="padding:24px"><h2>真实 UE 截图：用于诊断，非成功挤压演示</h2><div style="display:grid;grid-template-columns:repeat(auto-fit,minmax(280px,1fr));gap:16px">'
for candidate,pose in [('candidate-1','knee-120'),('candidate-3','knee-120-soft'),('candidate-3','standing-soft'),('candidate-4','knee-120-soft')]:
 sub='editor' if candidate=='candidate-1' else 'pie-soft';image=r/candidate/sub/pose/'side-gray.png'
 if image.exists():gallery+=f'<figure><img style="width:100%" src="{image.relative_to(r).as_posix()}"><figcaption>{html.escape(candidate+" / "+pose)}</figcaption></figure>'
gallery+='</div><p><a href="capture-summary.json">实际骨骼角度与状态</a> · <a href="regression-results.json">自动回归结果</a> · <a href="native-surface-regression.json">接触关闭表面回归</a></p></section>'
p=r/'report.html';p.write_text(p.read_text(encoding='utf-8').replace('</body>',gallery+'</body>'),encoding='utf-8');print(p)
