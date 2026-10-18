"""Publish real fine-only captures and measured lifecycle/performance evidence."""
import html,json,shutil,subprocess,sys,hashlib
from pathlib import Path

root=Path(__file__).resolve().parents[1]
out=root/'Evidence/HairGeneric';out.mkdir(parents=True,exist_ok=True)
names={'ponytail':'高马尾','long':'侧披长发','bun':'原盘发'}
panels=[];rows=[];identities=[];contacts=[]
for key,name in names.items():
    run=root/'Saved/HairGeneric'/f'{key}-r8';dest=out/key;dest.mkdir(exist_ok=True)
    bundle=json.loads((run/'bundle.json').read_text())
    capture=json.loads((run/'full/capture.json').read_text())
    editor=json.loads((run/'editor/check.json').read_text())
    pie=json.loads((run/'pie/capture.json').read_text())
    benchmark='benchmark-clean2' if (run/'benchmark-clean2/summary.json').exists() else 'benchmark'
    perf=json.loads((run/benchmark/'summary.json').read_text())
    assert bundle['independent_reload_verified'] and editor['status']=='passed'
    assert capture['frames']==720 and capture['actor']==bundle['blueprint']+'_C'
    assert pie['world']=='PIE' and pie['frames']==180
    assert all(g['self_collision_viscosity']==0 for f in capture['gpu_samples'] for g in f['groups'])
    for filename in ['bundle.json','editor/check.json','benchmark/summary.json']:
        shutil.copy2(run/filename,dest/(filename.replace('/','-')))
    shutil.copy2(run/benchmark/'summary.json',dest/'benchmark-summary.json')
    for filename in ['frames.csv','protocol.txt','process.json']:
        shutil.copy2(run/benchmark/filename,dest/('benchmark-'+filename))
    (dest/'pie.json').write_text(json.dumps({k:pie[k] for k in ['world','actor','frames','hair']},indent=2))
    video=dest/'fine.mp4'
    if not video.exists():
        subprocess.run(['ffmpeg','-y','-loglevel','error','-framerate','30','-c:v','mjpeg','-i',str(run/'full/video/frame-%04d.png'),'-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(video)],check=True)
    for n in [135,450,630]:shutil.copy2(run/f'full/video/frame-{n:04d}.png',dest/f'frame-{n}.jpg')
    before=f'../HairVariants/{key}/fine.mp4' if key!='bun' else '../HairCollisionRepair/final-fine.mp4'
    panels.append(f'<h3>{name}</h3><button onclick="syncPair(\'{key}\')">从头同步播放</button><div class="pair" id="pair-{key}"><figure><video controls preload="metadata" src="{before}"></video><figcaption>之前的精细档（历史录像）</figcaption></figure><figure><video controls preload="metadata" poster="{key}/frame-135.jpg" src="{key}/fine.mp4"></video><figcaption>本轮精细档 · 24 秒环绕运动</figcaption></figure></div><div class="shots">'+''.join(f'<img src="{key}/frame-{n}.jpg">' for n in [135,450,630])+'</div>')
    p=perf['phases']['fine']
    rows.append(f"| {name} | {p['mean_ms']:.2f} ms / {p['mean_fps']:.1f} FPS | {p['p95_ms']:.2f} ms | {perf['hair_delta_ms']['fine']:.2f} ms | {perf['off_drift_ms']:.2f} ms |")
    identities.append(dict(style=key,identity=bundle['identity'],blueprint=bundle['blueprint'],editor=editor['status'],pie_frames=pie['frames'],capture_sha256=hashlib.sha256((run/'full/capture.json').read_bytes()).hexdigest()))
    for script,target in [('audit_hair_temporal.py','temporal.json')]:
        subprocess.run([sys.executable,str(root/'Scripts'/script),str(run/'full/capture.json'),str(dest/target)],check=True,stdout=subprocess.DEVNULL)
    if key!='bun':
        before_skin=root/'Saved/HairGeneric'/f'{key}-baseline-skin/summary.json'
        after_skin=run/'full/skin/summary.json'
        a=json.loads(before_skin.read_text())['summary']['render'];b=json.loads(after_skin.read_text())['summary']['render']
        contacts.append(f"| {name} | {a['max_depth_cm']:.3f} → {b['max_depth_cm']:.3f} cm | {a['max_inside_over_02cm']} → {b['max_inside_over_02cm']} |")
        shutil.copy2(before_skin,dest/'before-head-skin.json');shutil.copy2(after_skin,dest/'after-head-skin.json')
(out/'identities.json').write_text(json.dumps(identities,indent=2))
body_run=root/'Saved/HairGeneric/long-r8/body-motion'
body_video=out/'long/body-motion.mp4'
if not body_video.exists():
    subprocess.run(['ffmpeg','-y','-loglevel','error','-framerate','30','-c:v','mjpeg','-i',str(body_run/'video/frame-%04d.png'),'-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(body_video)],check=True)
panels.append('<h3>长发补充：全身环绕</h3><video controls preload="metadata" style="width:100%" src="long/body-motion.mp4"></video><p>相同动作，扩大取景以检查肩背和身体接触。末帧另外导出了 A1 分层网格；该导出不能替代整段动态皮肤碰撞验证。</p>')
shutil.copy2(body_run/'body-audit-path.txt',out/'long/body-audit-path.txt')
for name in ['compile-07.log','unit-tests.log','long-baseline-body.json']:
    shutil.copy2(root/'Saved/HairGeneric'/name,out/name)
shutil.copy2(root/'Saved/HairGeneric/long-r8/full/body-contacts.json',out/'long/after-body-proxies.json')
draft='''---
title: 长发与马尾：通用物理链路修复
subtitle: 精细单档 · 原生 Groom / Niagara · VaM 源支撑与可见导线
lang: zh
---

## A · 这轮改了什么

保留已还原的渲染曲线、宽度、头发资源和组合资产。恢复身体碰撞代理的原始体积；从可见发型覆盖采样模拟导线；按源三角发束与长度关系转移支撑；扎发的收束支撑与自由尾部分开。头发自碰撞保持关闭。

粗略档已停止生成和维护。旧序列化值兼容迁移到 Fine，新录制只有精细档。

**这是经过实跑的改进版本，不是“任意发型、所有动作零穿模”的证明。** 头部采样仍有残留穿入；身体球/胶囊代理统计不等同真实皮肤穿入深度。请结合下面完整视频验收轮廓、束根和运动。

## B · 真实运动对比

同一人物、体型和已记录骨骼运动，起步、行走、跑步、转身、急停、跳跃和下蹲。世界位移来自已有重放轨迹，不是实时 CharacterMovement 导航。相机环绕；臀腿 Jiggle 开启。视频编码 30 FPS，模拟步长 1/60 秒，编码帧率不用于性能结论。

[[VIDEOS]]

## C · 根因与换路依据

| 发现 | 处理 |
|---|---|
| 旧构建为了避开静止头发，缩小胸背等非头部代理，破坏身体覆盖 | 保留原身体 PhysicsAsset 体积，只复制为头发碰撞缓存 |
| VaM 内部控制线经过长度、分束等操作后与可见曲线不同 | 从可见曲线覆盖采样，保留源三角发束来源以转移支撑 |
| 将每条内部控制线就近替换，会重复选中少量发丝 | 撤回，改为按可见形状覆盖采样；导线预算不增加 |
| 仅用全局 bend stiffness 无法表达扎点和发髻 | 逐节点源支撑、源连接的稀疏近似，加上有源连接和几何收束证据的扎点支撑 |
| 空间距离插值实拍让扎束变差 | 撤回，使用 UE 高质量形状搜索与 Parametric 插值 |

源点支撑和稀疏连接是对 VaM 数据语义的适配，不是逐帧等价复刻 VaM 求解器。扎点识别是基于源连接、支撑和几何收束的通用启发式，不含发型名称分支；对尚未覆盖的特殊造型仍需要回归。

## D · 实测性能

RTX 4070 Ti / i5-12600KF，1920×1080，单人物，无截图、无 GPU 审计读回；hair off → fine → off，每段预热 180 帧、测量 300 帧。基准使用根节点摇摆与转向，不是 24 秒录像整段的无录屏帧率。数值是整个测试场景帧时间，增量包含头发渲染和求解，并非孤立 GPU solver 时间。

| 发型 | 精细平均帧时间 / FPS | P95 | 头发总增量 | 前后空载漂移 |
|---|---|---|---|---|
'''+ '\n'.join(rows)+'''

## E · 实际运行与证据

三套新身份均独立进程重载。正常 BP_VamCharacter 完成 Editor 挂接/激活检查、24 秒 Game 运动，以及 6 秒 PIE 重放。PIE 短片不冒充完整 24 秒 PIE 验证。所有采样的头发自碰撞 viscosity 为 0。相关 Python 回归 29 项通过；UE 模块已实际编译并安装，最终 C++ 构建见 compile-07.log。

[资产身份与 BP 路径](identities.json)。各发型目录保存 bundle、Editor、PIE、性能和逐帧运动指标。Python 回归测试、编译日志和未采用试验保存在 Saved/HairGeneric，未覆盖历史发型。

实现提交：`6937218`（单档迁移）、`7fe0075`（导线覆盖、源支撑和完整身体代理），均已推送 feature/hair。新角色资产独立生成，未自动替换当前关卡中的角色。[编译日志](compile-07.log) / [29 项测试日志](unit-tests.log)。

## E2 · 未隐藏的碰撞残留

以下对照均取 0、2、8、12、16、20、23 秒，覆盖相同的起步、跑步、跳跃和下蹲片段。使用同一 A1 头部表面、相同采样方法；排除发根前 10%，统计最终渲染发丝，不只是模拟导线。这不是全身动态皮肤验收，也不是每根发丝每一帧的穷举。

| 发型 | 头部内部深度峰值（旧 → 新） | 单次采样超过 2 mm 的点数峰值（旧 → 新） |
|---|---|---|
'''+ '\n'.join(contacts)+'''

长发另外按新版本完整身体代理重新评估了历史发丝，保证没有拿旧的缩小代理作弊：渲染点的代理重叠峰值约 13.23 → 5.03 cm，超过 2 mm 的采样点峰值 1218 → 246。**残余 5.03 cm 是胶囊/球并集重叠，不能解释为真实皮肤穿入 5.03 cm，也不能当成已解决。** 全身视角用于补充视觉核查。

目前仍有少量脸部/头部穿入，且代理不能覆盖所有精细身体轮廓；本轮改善了基础导线、支撑与身体输入，但没有宣称碰撞问题彻底清零。Rigged Guides / Deformer Graph 是后续复杂扎发的一条独立备选，不应继续只提高迭代次数。

## F · 参考与适用边界

- [Epic Groom 导入：Generated / Imported / Rigged Guides](https://dev.epicgames.com/documentation/en-us/unreal-engine/importing-grooms-into-unreal-engine)：本轮采用从可见曲线构造导线的方向，保留本项目的源支撑转移。
- [Epic Hair Art Directability 与 Content Examples](https://dev.epicgames.com/documentation/metahuman/hair-art-directability?lang=en-US)：骨骼驱动导线是复杂扎发的另一条路线，本轮没有宣称已接入它。
- [Netrunner 实际制作中的身体碰撞修复](https://www.provos.org/p/netrunner-music-video-v2/)：检查 Groom 的身体碰撞组件来源。当前实现已具备身体输入；本项目额外的问题是缩小了代理。
- [SideFX：卷发、辫发与 UE Groom 工作流](https://www.sidefx.com/tutorials/advanced-grooming-tips-and-houdini-to-unreal-engine-tutorial/)：复杂造型需要保留分束结构，不能只靠提高整体刚度。
- [Holding the Shape in Hair Simulation，SIGGRAPH 2019](https://history.siggraph.org/wp-content/uploads/2022/09/2019-Talks-Iben_Holding-the-Shape-in-Hair-Simulation.pdf)：形状支撑与自由运动需要分别表达。

VaM 依据来自用户本地原版运行时数据、点支撑与 NearbyDistanceJoints 调用逻辑。没有将源 NearbyJointPower=0 的保留编辑数据误启用，也没有把源数据中的未使用 Gravity 字段当成有效重力。
'''
(out/'draft.md').write_text(draft,encoding='utf-8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(out/'draft.md'),'--output',str(out/'report.html'),'--no-open'],check=True)
page=(out/'report.html').read_text(encoding='utf-8').replace('[[VIDEOS]]',''.join(panels))
page=page.replace('</head>','<style>.pair{display:grid;grid-template-columns:1fr 1fr;gap:14px}.pair figure{margin:0}.pair video,.shots img{width:100%}.shots{display:grid;grid-template-columns:repeat(3,1fr);gap:8px;margin:12px 0}@media(max-width:700px){.pair,.shots{grid-template-columns:1fr}}</style></head>')
page=page.replace('</body>',"<script>function syncPair(k){const vs=document.querySelectorAll('#pair-'+k+' video');vs.forEach(v=>{v.pause();v.currentTime=0;v.play().catch(()=>{});});}</script></body>")
(out/'report.html').write_text(page,encoding='utf-8')
print(out/'report.html')
