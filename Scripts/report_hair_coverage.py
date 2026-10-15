"""Package real-engine side coverage evidence without modifying screenshots."""
import json,shutil,subprocess,sys
from pathlib import Path

repo=Path(__file__).resolve().parents[1];src=repo/'Saved/HairCoverage';out=repo/'Evidence/HairCoverage'
out.mkdir(parents=True,exist_ok=True)
bundle=json.loads((src/'bundle.json').read_text());audit=json.loads((src/'width-audit.json').read_text())
capture=json.loads((src/'game-host/capture.json').read_text())
assert bundle['independent_reload_verified'] and all(not g['simulation'] for g in capture['groups'])
for name in ('bundle.json','width-audit.json','tests.txt'):shutil.copy2(src/name,out/name)
shutil.copy2(src/'game-host/capture.json',out/'capture.json')
shutil.copy2(src/'game/capture.json',out/'main-capture.json')
views={'left':'左侧（重点）','right':'右侧','front':'正面','rear':'后面','oblique':'斜视'}
for name in views:shutil.copy2(src/'game-host'/(name+'.png'),out/(name+'.png'))
rows='\n'.join(f"| {g['name']} | {g['before_diameter_cm']} | {g['after_diameter_cm']} | {g['render_curves']} |" for g in audit['groups'])
doc=f'''---
title: 两侧显稀：发丝半宽／直径转换修复
subtitle: 真实 VaM 与 UE 五视角 · 物理关闭 · 同条件修复前后对照
lang: zh
---
已证实旧转换把源发丝半宽当成 UE 直径，导致实际渲染宽度减半。修正转换后重新生成普通角色和 Groom；头发源资源及发型组合不变。

## A · 根因与修复
源渲染先产生宽度向量，随后生成中心线 + 宽度向量、中心线 − 宽度向量两侧顶点；源 FixedWidth 是半宽。UE Groom 的 HairWidth 在 HairStrandsInterpolation.cpp 中乘以 0.5，得到渲染半径。因此正确换算为：UE 直径 cm = 源半宽 m × 100 × 2。

原来的转换只有 ×100。`prepare_hair_render_reference.py` 已修复单位语义，增加无效宽度拒绝和回归测试。不是只给左侧设置特例，也没有通过新增发束或移动发际线填补空隙。

## B · 改动范围
| 资源 | 旧直径 cm | 修正直径 cm | 渲染曲线数 |
|---|---:|---:|---:|
{rows}

逐数组核对：38,923 条渲染曲线的中心线完全相同；独立头发 catalog 与发型组合完全相同。仅新角色适配缓存使用正确宽度。发型物理仍关闭。

## C · 实跑与数据
- 主工程资产生成、独立 Editor 重载通过。
- 15 项 Python 测试通过，包括宽度单位与无效输入。
- 主工程与隔离工程普通 BP 的五视角 Game 截图均完成，四组发型物理均关闭。下方比较采用与上轮相同的隔离工程，排除主工程渲染设置差异；[主工程运行状态](main-capture.json)另存。
- [宽度和中心线核对](width-audit.json) · [测试输出](tests.txt) · [运行状态](capture.json) · [资产清单](bundle.json)。
- 新角色：`{bundle['blueprint'].split('.')[0]}`。

## D · 对比的边界
修复前后 UE 使用同一相机、光源、曝光和体型；没有修改材质、光源或背景。VaM 的发簪、口罩与衣服属于独立配饰，不能将其遮挡计为头发覆盖。不同引擎的 shader、透明覆盖和抗锯齿仍不同，保留原始截图供视觉判断。

本轮修复作用于已验证的 VaM 静态导出路径；未宣称纯文件解析已完整还原任意发型。新资产已在主工程生成，旧资产保留，便于回退。
'''
(out/'report.md').write_text(doc,encoding='utf-8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(out/'report.md'),'--output',str(out/'report.html'),'--no-open'],check=True)
gallery='<section class="hair-gallery"><h2>真实截图：VaM / 修复前 / 修复后</h2><p>点击图片打开原尺寸。左右侧优先排列。</p>'
for name,title in views.items():
    gallery+=f'<h3>{title}</h3><div class="hair-row">'
    for url,label in [(f'../HairRestoration/vam/{name}.png','VaM'),(f'../HairRestoration/after/{name}.png','UE 修复前'),(f'{name}.png','UE 修复后')]:
        gallery+=f'<figure><a href="{url}"><img src="{url}" loading="lazy" alt="{label} {title}"></a><figcaption>{label}</figcaption></figure>'
    gallery+='</div>'
gallery+='</section>'
p=out/'report.html';text=p.read_text(encoding='utf-8');text=text.replace('</head>','<style>.hair-gallery{max-width:1500px;margin:30px auto;padding:24px}.hair-row{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.hair-row figure{margin:0}.hair-row img{width:100%;display:block}.hair-row figcaption{padding:10px}@media(max-width:700px){.hair-row{grid-template-columns:1fr}}</style></head>');p.write_text(text.replace('</body>',gallery+'</body>'),encoding='utf-8');print(p)
