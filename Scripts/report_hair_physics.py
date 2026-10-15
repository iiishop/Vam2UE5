"""Package original UE frames and synchronized motion clips for visual review."""
import argparse
import json
import shutil
import subprocess
import sys
from pathlib import Path
from audit_hair_motion import summarize

p = argparse.ArgumentParser()
p.add_argument('run', type=Path)
p.add_argument('bundle', type=Path)
p.add_argument('output', type=Path)
p.add_argument('--suffix', default='')
a = p.parse_args()
out = a.output.resolve()
out.mkdir(parents=True, exist_ok=True)
bundle = json.loads(a.bundle.read_text())
assert bundle['independent_reload_verified']
shutil.copy2(a.bundle, out/'bundle.json')
for name in ('tests.txt', 'rest-identity-audit.json'):
    shutil.copy2(a.run/name, out/name)
ffmpeg = shutil.which('ffmpeg')
assert ffmpeg

def encode(args):
    with (out/'encoding.log').open('a') as log:
        subprocess.run([ffmpeg, '-y', *args], stdout=log, stderr=subprocess.STDOUT, check=True)

metrics = {}
for tier in ('fine', 'coarse', 'rest', 'full', 'pie'):
    take = a.run/(tier+a.suffix)
    capture = take/'capture.json'
    assert capture.exists(), capture
    metrics[tier] = summarize(capture)
    (out/(tier+'-audit.json')).write_text(json.dumps(metrics[tier], indent=2))
    assert metrics[tier]['frames'] == (180 if tier == 'pie' else 720)
    if tier == 'pie':
        assert metrics[tier]['world'] == 'PIE'
        shutil.copy2(take/'video/frame-0120.png', out/'pie.png')
        continue
    magic = (take/'video/frame-0000.png').read_bytes()[:8]
    codec = 'mjpeg' if magic[:3] == b'\xff\xd8\xff' else 'png'
    encode(['-framerate', '30', '-c:v', codec, '-i', str(take/'video/frame-%04d.png'),
            '-c:v', 'libx264', '-preset', 'fast', '-crf', '20', '-pix_fmt', 'yuv420p',
            '-movflags', '+faststart', str(out/(tier+'.mp4'))])
    shutil.copy2(take/'video/frame-0000.png', out/(tier+'.png'))
encode(['-i', str(out/'fine.mp4'), '-i', str(out/'coarse.mp4'),
        '-filter_complex', 'hstack=inputs=2', '-c:v', 'libx264', '-preset', 'fast',
        '-crf', '20', '-pix_fmt', 'yuv420p', '-movflags', '+faststart', str(out/'comparison.mp4')])
rows = []
for tier in ('fine', 'coarse'):
    records = metrics[tier]['measurements']
    for group in sorted({r['group'] for r in records}):
        selected = [r for r in records if r['group'] == group]
        assert all(r['simulation'] for r in selected)
        rows.append(f"| {tier} | {group} | {selected[0]['curves']} | {selected[0]['rest_rms_cm']:.3f} | {max(r['motion_rms_cm'] for r in selected):.3f} | {max(r['rest_max_cm'] for r in selected):.3f} |")

doc = f'''---
title: 头发物理：精细 / 粗略双档
subtitle: 原生 UE Groom + 梳理形状约束 · 真实角色动作录像
lang: zh
---
本页提供同一角色、同一骨骼动作、相机和灯光的精细／粗略／关闭物理对照。视频为真实 UE 截图序列，30 fps 编码不代表游戏性能达到 30 fps。

## A · 实现路径
独立头发资源与发型组合保持原样，人物引用的适配缓存保存两档 Groom、材质和头发专用碰撞资产。38,923 条渲染曲线中心线、宽度及资源 catalog 均与上一轮相同；修改的是物理导向线和求解设置。

从原版静态导出获取曲线静止形状，再根据盘绕程度、源刚性信息自动区分支撑、紧凑盘绕、松散三类。代码没有当前发型名称或资源 ID 分支。原生 Cosserat Rods 求解拉伸、弯曲、惯性与碰撞，新增形状约束直接写回求解状态，而非只修改最后渲染位置。

## B · 两档区别
| 项目 | 精细 | 粗略 |
|---|---:|---:|
| 每组导向线预算 | 128 | 32 |
| 每条模拟节点 | 32 | 16 |
| 子步 / 迭代 | 5 / 8 | 3 / 4 |
| 渲染发丝 | 38,923 | 38,923 |
| 分组物理 | 开启 | 开启 |

束质量按代表的发丝总长度与截面积换算，避免减少导向线后整组质量随预算下降。弯曲刚度做相应束截面补偿。盘绕组、贴头组与散发使用不同形状约束，粗略档并非整顶头发作为一个刚体。

## C · 两个实测问题
只提高原生弯曲刚度仍会使部分盘绕导向线在重力下散开。严格关闭重力与碰撞后，初始 GPU 导向线可以保持原形，因此增加了求解内的梳理形状约束。

身体的粗碰撞代理还会把散发向外撑开。运行时碰撞资产来自 RuntimeConfiguration，而不是 Body Mesh 的默认资产；构建时现在明确传入这一资产，再创建独立副本，根据静止发束与胶囊／球代理的距离作保守半径拟合。原角色身体碰撞资产没有被修改。具体半径记录在资产清单的 collision_fit 中。

## D · 真实 GPU 读回
位移单位 cm。第一项是预热后相对静止导向线的 RMS，第二项是运动时相对首帧的最大采样 RMS，第三项是全程相对静止形状的最远采样点。每秒读回一次，不能据此排除采样之间的所有瞬时问题。

| 档位 | 组 | 导向线 | 静止 RMS | 运动 RMS 最大 | 静止形状偏移 Max |
|---|---:|---:|---:|---:|---:|
{chr(10).join(rows)}

## E · 动作与运行
0–1 s 静止；1–2 s 起步；2–5 s 行走；5–7 s 转身；7–8 s 加速；8–11 s 跑步；11–11.4 s 急停；随后保留跳跃、蹲起、再次行走和急停。沿用已保存的实际骨骼矩阵和角色位移，臀腿 Jiggle 均开启。这是可重复动画回放，不是 CharacterMovement 导航测试。

普通 BP 的 Game 录像与真实 PIE 预览分别保存。资产通过独立 Editor 进程重载。测试输出：[19 项头发测试](tests.txt)。[精细读回](fine-audit.json) · [粗略读回](coarse-audit.json) · [PIE 读回](pie-audit.json) · [静态资源一致性](rest-identity-audit.json)。

## F · 使用与边界
新普通角色：`{bundle['blueprint'].split('.')[0]}`。
Hair 组件的 `SetHairQuality` 切换 Fine/Coarse，`SetHairSimulationEnabled` 控制物理，`ResetHairSimulation` 重置状态。切换档位会重建该档求解状态。

形状约束频率和位移限制是工程控制参数，不宣称是测量得到的人发材料常数。当前分类以源资源组为单位；本轮只实测当前四资源发型，未宣称任意发型已逐一通过。强烈混合扎紧区和自由区的单一资源、缺少可靠刚性信息的资源，仍需进一步分区验证。碰撞是保守球／胶囊近似，不是精确头皮 SDF，也没有完整发丝自碰撞与双向身体受力。

参考：[Epic Groom 物理设置](https://dev.epicgames.com/documentation/unreal-engine/enabling-physics-simulation-on-grooms-in-unreal-engine)。
'''
(out/'report.md').write_text(doc, encoding='utf-8')
renderer = Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable, '-X', 'utf8', str(renderer), 'render', str(out/'report.md'), '--output', str(out/'report.html'), '--no-open'], check=True)
media = '''<section class="media"><h2>同步对比：左精细，右粗略</h2><video controls preload="metadata" src="comparison.mp4"></video><h2>第三人称完整动作（精细）</h2><video controls preload="metadata" src="full.mp4"></video><h2>关闭物理对照</h2><video controls preload="metadata" src="rest.mp4"></video><h2>同条件首帧：关闭物理 / 精细 / 粗略</h2><div class="row">'''
for tier in ('rest', 'fine', 'coarse'):
    media += f'<figure><a href="{tier}.png"><img src="{tier}.png" alt="{tier}"></a><figcaption>{tier}</figcaption></figure>'
media += '</div><h2>真实 PIE 预览</h2><img src="pie.png" alt="PIE"></section>'
page = (out/'report.html').read_text(encoding='utf-8')
page = page.replace('</head>', '<style>.media{max-width:1500px;margin:30px auto;padding:24px}.media video,.media img{width:100%;display:block}.row{display:grid;grid-template-columns:repeat(3,1fr);gap:12px}.row figure{margin:0}@media(max-width:700px){.row{grid-template-columns:1fr}}</style></head>')
(out/'report.html').write_text(page.replace('</body>', media+'</body>'), encoding='utf-8')
print(out/'report.html')
