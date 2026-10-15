import json,subprocess,hashlib
from pathlib import Path
r=Path(__file__).resolve().parents[3]/'Evidence/LocomotionSuite'
a=json.loads((r/'analysis.json').read_text());identity=json.loads((r/'asset-identity.json').read_text())
md=f'''---
title: 正常朝向 · 连续走跑转跳蹲
subtitle: 2026-10-15 · 真实 UE PIE · 臀部和腿部 Jiggle 开启 · 24 秒完整动作
lang: zh
---

## A 本次修正

之前斜着移动来自录制重定向：骨盆使用 Idle 首帧作零点，其他链条使用不同基准；前方向又从单侧脚趾估计。本次统一躯干、腿部的参考骨架旋转基准，以双侧髋关系确定前方向，根平移保持地面水平。手臂单独匹配实际肢段方向，避免源 A-pose 与目标 T-pose 差异造成手臂外张。

臀腿 Jiggle 继续使用已经发布的 LegResearch 参数。没有通过减少晃动来掩盖动画朝向错误。

## B 连续动作

| 时间 | 动作 |
| --- | --- |
| 0–2 秒 | 站立、起步 |
| 2–5 秒 | 行走 1.6 m/s |
| 5–7 秒 | 行走中转身 90° |
| 7–11 秒 | 加速至跑步 4 m/s |
| 11–12 秒 | 急停与余振 |
| 12–14 秒 | 预备、跳跃、落地缓冲 |
| 15–18.8 秒 | 蹲下、保持、起身 |
| 19–24 秒 | 再次行走、停下、恢复 |

整个序列一次连续模拟，没有拼接不同角色镜头。采用右肩后方跟随相机。主光、补光随角色方向转动，保证转身后仍能观察臀腿；地面标记为固定世界位置。

## C 实际记录

| 项目 | 结果 |
| --- | --- |
| UE 世界 | {a['world']} |
| 帧数 / 时长 | {a['frames']} / {a['duration']:.0f} 秒 |
| 行走平均骨盆偏航 | {a['heading_metrics']['walk']['pelvis_yaw_mean_deg']:.2f}° |
| 跑步平均骨盆偏航 | {a['heading_metrics']['run']['pelvis_yaw_mean_deg']:.2f}° |
| 累计行进 | {a['distance_cm']/100:.2f} m |
| 转向 | {a['turn_degrees']:.1f}° |
| 跳跃根位移峰值 | {a['jump_height_cm']:.2f} cm |
| 骨骼矩阵有限值 | {a['finite_matrices']} |
| 臀 / 腿 Jiggle | {a['glute_jiggle']} / {a['leg_jiggle']} |
| Jiggle 限位修正计数 | {a['limit_corrections']} |

偏航是骨盆相对角色前方向的测量；步态包含正常的周期性骨盆旋转，不要求每帧为零。原录像行走、跑步平均约 −20° / −22°。生产关节限位仍生效：录制中累计 {a['retarget_clamp_count']} 次骨骼请求受限，最大修正 {a['max_clamp_degrees']:.2f}°。

## D 验证范围

本次是真实 BP_VamCharacter 的 PIE 渲染，使用原生产角色资产；UE 编译与录制自动测试均保留日志。成片 30 fps，模拟固定步长 1/60；这不是实时性能测量。

行走、跑步、跳跃姿态来自 UE 自带动画。工程没有现成蹲下动画，蹲起由连续混合的骨骼姿态生成。世界移动与跳跃轨迹由录制脚本驱动，尚不能作为 CharacterMovement、脚底锁定或碰撞落地系统的验收。Contact 关闭，以便观察已有 Jiggle。

资产身份：`{identity['identity']}`。

[关键帧](keyframes.jpg) · [数据分析](analysis.json) · [逐帧骨骼和求解器记录](locomotion.json.gz) · [录制命令](command.json) · [编译日志](compile.log) · [UE 日志](runtime.log.gz)
'''
(r/'report.md').write_text(md,encoding='utf-8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run(['F:/Program/Python311/python.exe','-X','utf8',str(renderer),'render',str(r/'report.md'),'--output',str(r/'report.html'),'--no-open'],check=True)
h=(r/'report.html').read_text(encoding='utf-8')
v='<section style="max-width:1200px;margin:20px auto;padding:20px;background:#18202a;color:white;border-radius:16px"><h2>完整动作 · 24 秒 · 臀腿 Jiggle 开启</h2><video id="v" controls playsinline preload="metadata" poster="walk.jpg" style="width:100%" src="media/continuous.mp4"></video><p>'
for t,label in [(0,'起步'),(3,'行走'),(5,'转身'),(8,'跑步'),(11,'急停'),(12,'跳跃'),(15,'蹲起')]:v+=f'<button style="padding:9px;margin:4px" onclick="var v=document.getElementById(\'v\');v.currentTime={t};v.play()">{label}</button>'
v+='</p><details><summary>半速检查（无插帧）</summary><video controls playsinline preload="none" style="width:100%" src="media/half-speed.mp4"></video></details><h2>动作关键帧</h2><img src="keyframes.jpg" style="width:100%"></section>'
(r/'report.html').write_text(h.replace('</header>','</header>'+v,1),encoding='utf-8')
(r/'manifest.json').write_text(json.dumps({str(p.relative_to(r)):hashlib.sha256(p.read_bytes()).hexdigest() for p in r.rglob('*') if p.is_file() and p.name!='manifest.json'},indent=2))
