"""Publish actual VaM simulated references, with measured-time three-view videos."""
import json,hashlib,shutil,subprocess,sys,html
from pathlib import Path
from audit_vam_hair_dynamic import audit
ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'Evidence/HairDynamicReference'
OUT.mkdir(parents=True,exist_ok=True)
panels=[];rows=[];identities=[]
for style,label in [('ponytail','高马尾'),('long','长发')]:
    run=ROOT/f'Saved/HairRestoration/dynamic-{style}-01'
    source=run/'runtime';dest=OUT/style;dest.mkdir(exist_ok=True)
    if not (run/'restored.txt').exists():raise RuntimeError('VaM plugins not yet restored')
    metrics=audit(source)
    if not metrics['complete']:raise RuntimeError('Incomplete source capture')
    frames=json.loads((source/'dynamic-samples.json').read_text())
    (dest/'metrics.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2),encoding='utf8')
    for name in ['dynamic-manifest.json','dynamic-samples.json','progress.log','complete.txt']:
        shutil.copy2(source/name,dest/name)
    shutil.copy2(run/'restored.txt',dest/'restored.txt')
    manifest=json.loads((source/'dynamic-manifest.json').read_text())
    preset=Path(manifest['hair_preset'])
    identities.append({'style':style,'preset':str(preset),'preset_sha256':hashlib.sha256(preset.read_bytes()).hexdigest(),
                       'capture_manifest_sha256':hashlib.sha256((source/'dynamic-manifest.json').read_bytes()).hexdigest(),'raw_capture':str(source)})
    panels.append(f'<h3>{label}：原版 VaM 物理运行</h3><div class="views">')
    for view,title in enumerate(['正面','后侧 A','后侧 B']):
        ids=list(range(view,len(frames),3));concat=dest/f'view-{view}.txt'
        lines=[]
        for j,i in enumerate(ids):
            image=source/f'frame-{i:04d}.jpg'
            duration=float(frames[ids[j+1]]['time'])-float(frames[i]['time']) if j+1<len(ids) else .2
            lines += ["file '"+image.as_posix()+"'",f'duration {duration:.8f}']
        lines += ["file '"+(source/f'frame-{ids[-1]:04d}.jpg').as_posix()+"'"]
        concat.write_text('\n'.join(lines),encoding='utf8')
        subprocess.run(['ffmpeg','-y','-loglevel','error','-f','concat','-safe','0','-i',str(concat),'-vf','fps=30','-c:v','libx264','-crf','20','-pix_fmt','yuv420p','-movflags','+faststart',str(dest/f'view-{view}.mp4')],check=True)
        panels.append(f'<figure><video controls preload="metadata" src="{style}/view-{view}.mp4"></video><figcaption>{title}；按实际模拟时间编码，原始有效采样约 5 FPS/视角。</figcaption></figure>')
    panels.append('</div><div class="views">')
    for t,title in [(7,'物理沉降后'),(13,'转头中'),(23,'停止恢复后')]:
        ids=[i for i in range(2,len(frames),3)]
        i=min(ids,key=lambda i:abs(float(frames[i]['time'])-t));name=f'{title}.jpg';shutil.copy2(source/f'frame-{i:04d}.jpg',dest/name)
        panels.append(f'<figure><img src="{style}/{name}"><figcaption>{title}，t={float(frames[i]["time"]):.2f}s</figcaption></figure>')
    panels.append('</div>')
    for g in metrics['groups']:
        p=g['phases'];rows.append(f'| {g["path"].split(":")[-1]} | {g["strands"]} | {p["motion"]["tip_excursion_max_cm"]:.2f} | {p["recovery"]["tip_excursion_max_cm"]:.2f} | {p["recovery"]["tip_speed_final_median_cm_s"]:.4f} |')
(OUT/'identities.json').write_text(json.dumps(identities,indent=2),encoding='utf8')
draft='''---
title: 长发与马尾：VaM 原生动态对照
subtitle: 实际 GPU 粒子 · 沉降 / 转头 / 停止回弹 · 精细档研究基线
lang: zh
---
## A 本轮结论

之前的 VaM 对照关闭了头发模拟，只能验证静态还原。本轮实际运行了原版头发求解器；同一 QiMeng-b 人物分别加载既有长发与马尾源预设，保留源头发约束参数，输出粒子轨迹与三个视角。

**本报告建立动态源基线并定位转换差异；尚未声称 UE 与 VaM 动态等价或已经完成新的 UE 求解器修复。** 历史 UE 录像动作不同，只能参考，不能与本次数据计算同动作误差。

## B 实际视频

0–8 秒静置沉降，8–16 秒头部摆动，16–24 秒停止恢复。身体采用运动学骨骼驱动、头发物理开启，非全身自由动力学。原版服装保留，但没有据此宣称衣服参与头发碰撞。启动最初帧包含求解器重置，不能当作沉降后的外形。

[[VIDEOS]]

## C 粒子运动证据

每帧用实际头骨世界矩阵的逆变换移除头部刚体运动；单位 cm。位移是该阶段相对第一帧的最大发梢位移，非画面像素。最后速度是阶段末五个采样间隔的中位数。GPU 读回会干扰帧耗时，本轮不作性能结论。

| 发块 | 源导线数 | 转头最大位移 cm | 恢复阶段最大位移 cm | 最终速度 cm/s |
|---|---|---|---|---|
'''+ '\n'.join(rows)+'''

## D VaM 如何维持发型并允许运动

本机 Assembly-CSharp 的 HairPhysicsWorld.DispatchPhysicsImpl 调用链确认：启动阶段先重置到造型支撑；随后迭代积分、PointJoints、DistanceJoints、可选 CompressionJoints、可选 NearbyDistanceJoints、SplineJoints，最后处理球/连接球/平面碰撞及点支撑。方法调度证据不能替代 GPU 内核数学公式证明。

| 机制 | 作用及当前 UE 差异 |
|---|---|
| 逐点 rigidity | 根端固定，绘制刚性或 root/main/tip 曲线控制对原造型的支撑；不是一个全局弯曲刚度 |
| Distance / Compression / Spline | 保持长度、压缩和沿发丝的形状；当前 UE 用 Rod 替代，未逐项等价映射 |
| NearbyDistanceJoints | 预先建立的发束连接；不同于运行时自碰撞。当前 UE 最多保留每点两条连接，不能视作完整等价 |
| Weight | VaM 在本机调度中将其用于积分时间尺度；不能直接把同一个数作为 UE 的质量参数 |
| 碰撞 | 源运行时使用球与连接球等代理；UE 使用 PhysicsAsset。需核对真实覆盖、更新时序及碰撞后造型投影 |

## E 当前 UE 应重点验证的差异

VamHairSourceConstraints.h 将点支撑折算为按 60Hz 和 UE 迭代次数重复的回形衰减；源与目标迭代语义不同，不能靠相同数值证明等价。

VamHairStyleSolver.h 还追加了回形正则和固定最大位移：loose 为 10 cm、supported 为 0.15 cm、compact 为 0.25 cm。当前马尾主体属于 loose，发际部分为 supported。VaM 实测自由尾部能出现超过 10 cm 的运动，但两边所用位移定义不同，因此这是优先消融项，尚非单项根因证明。

接下来用实际矩阵统一 UE 驱动，按顺序消融额外回形 / 位移上限、逐点支撑折算、发束连接；保持原始资源和碰撞开关不变。优先让束根保持造型、尾部自由弯曲，再处理长发与肩背接触；保留关闭头发自碰撞的要求。

## F 参考与数据边界

- [Epic Hair Physics Overview](https://dev.epicgames.com/documentation/unreal-engine/hair-physics-in-unreal-engine---overview)：UE 使用 GPU Niagara / XPBD，导线驱动渲染发丝，PhysicsAsset 代理处理身体碰撞。
- [Epic Groom Physics Settings](https://dev.epicgames.com/documentation/unreal-engine/enabling-physics-simulation-on-grooms-in-unreal-engine)：弯曲/拉伸、空气阻力、碰撞、局部模拟参数。
- [VaM 发型作者指南](https://hub.virtamate.com/resources/hairstyle-creation-guide-tips.423/)：复杂发型按发块组织，绘制刚性决定离开造型模式后的形状。属于作者经验指南，非完整求解器规范。

本次源调用链来自本机程序集只读检查；原插件在每次采样后恢复并校验哈希。身份见 identities.json；原始粒子、实际矩阵和时间戳路径在其中。历史静态证据保留。
'''
(OUT/'draft.md').write_text(draft,encoding='utf8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(OUT/'draft.md'),'--output',str(OUT/'report.html'),'--no-open'],check=True)
page=(OUT/'report.html').read_text(encoding='utf8').replace('[[VIDEOS]]',''.join(panels))
page=page.replace('</head>','<style>.views{display:grid;grid-template-columns:repeat(3,1fr);gap:10px}.views figure{margin:0}.views video,.views img{width:100%}@media(max-width:800px){.views{grid-template-columns:1fr}}</style></head>')
(OUT/'report.html').write_text(page,encoding='utf8')
print(OUT/'report.html')
