"""Package actual preset-import and UE motion evidence, with explicit limitations."""
import hashlib, html, json, shutil, subprocess, sys
from pathlib import Path

root=Path(__file__).resolve().parents[1];out=root/'Evidence/HairVariants';run=root/'Saved/HairVariants'
names={'long':'侧披长发 · long9 black','ponytail':'高马尾 · Swift Ponytail + Bangs A'}
records=[];table=[]
for key in names:
    bundle=json.loads((run/key/'bundle.json').read_text())
    assert bundle['independent_reload_verified']
    for quality in ['fine','coarse']:
        take=run/key/(quality+'-main');capture=json.loads((take/'capture.json').read_text())
        assert capture['frames']==720 and capture['world']=='Game' and capture['orbit_camera']
        assert capture['actor']==bundle['blueprint']+'_C'
        assert capture['glute_jiggle'] and capture['leg_jiggle']
        assert all(g['readback_ready'] and g['simulation'] for f in capture['gpu_samples'] for g in f['groups'])
        video=out/key/(quality+'.mp4')
        if not video.exists():
            subprocess.run(['ffmpeg','-y','-loglevel','error','-framerate','30','-c:v','mjpeg','-i',str(take/'video/frame-%04d.png'),'-c:v','libx264','-preset','fast','-crf','19','-pix_fmt','yuv420p','-movflags','+faststart',str(video)],check=True)
        probe=json.loads(subprocess.check_output(['ffprobe','-v','error','-select_streams','v:0','-show_entries','stream=nb_frames,duration,width,height','-of','json',str(video)]))['streams'][0]
        assert int(probe['nb_frames'])==720 and abs(float(probe['duration'])-24)<.01
        temporal=out/key/(quality+'-temporal.json')
        if not temporal.exists():
            subprocess.run([sys.executable,str(root/'Scripts/audit_hair_temporal.py'),str(take/'capture.json'),str(temporal)],check=True,stdout=subprocess.DEVNULL)
        summary=json.loads(temporal.read_text())['summary']
        q=next(q for q in bundle['build_report']['quality'] if q['quality'].lower().endswith(quality))
        peak=max(v['render']['max_step_cm'] for v in summary.values())
        table.append(f"| {names[key]} | {'精细' if quality=='fine' else '粗略'} | {q['guides']} × {q['groups'][0]['points_per_guide']} | {q['render_curves']:,} | {peak:.2f} cm |")
        records.append(dict(style=key,quality=quality,asset_identity=bundle['identity'],world=capture['world'],actor=capture['actor'],frames=capture['frames'],simulation_dt=capture['simulation_dt'],video_fps=capture['video_fps'],orbit_camera=True,glute_jiggle=True,leg_jiggle=True,source_motion_sha256=hashlib.sha256((root/'Saved/HairRuntime/qimeng-take-1/fine/locomotion.json').read_bytes()).hexdigest(),capture_sha256=hashlib.sha256((take/'capture.json').read_bytes()).hexdigest(),capture_path=str(take/'capture.json'),probe=probe))
(out/'recordings.json').write_text(json.dumps(records,indent=2))
draft='''---
title: 同一人物，换上长发和马尾
subtitle: 浏览器预设 → 独立头发资产 → 同一身体 → 真实 UE 运动
lang: zh
---

## A · 结果与边界

两套预设均已从插件浏览器使用的 Catalog / Planner 导入，生成独立头发资源、组合发型、两档 Groom 与正常 BP_VamCharacter。沿用同一身体、体型、骨骼和运动；原盘发未被覆盖。

**静态发型已经接入；两套动态都仍有问题。** 实拍中马尾扎束附近变松、折叠并局部露出头皮。长发能够随运动摆动，但原侧披造型会明显松散；头部表面审计还发现渲染发丝穿入。此次换发型证明了资产组合路径可用，也暴露了现有物理方案尚不能直接适配所有发型。

## B · 四段真实运动录像

每段 24 秒、720 帧、1280×720，包含原有起步、走路、跑步、转身、急停、跳跃和下蹲运动，镜头绕一圈。两档使用完全相同的源骨骼运动和镜头。臀腿 Jiggle 开启，软体 Contact 关闭。

**30 fps 是视频编码帧率，模拟按 1/60 秒推进。本次没有新增无录屏开销的性能基准，不能据此宣称实际游戏帧率。**

[[VIDEOS]]

## C · VaM 与 UE 的静态发型核对

两边均关闭头发物理，用实测头骨矩阵转换相机。几何对应；UE Groom 与 VaM 的发丝着色、曝光响应不同，不以颜色像素差宣称几何误差。VaM 原人物附带衣服、面罩和发饰，UE 此次只更换选定的头发资源。

[[GALLERY]]

## D · 动态发现：这次没有隐藏失败

马尾在第 6–8 秒侧面可见束根松散与折叠，原本贴头的区域出现不自然分层。以下都是实际录像帧。

[[FAILURES]]

源码和源数据确认：马尾主体保存了 37,284 条附近发束关系，发际线有 1,315 条；当前 Builder 没有把这些关系转为求解约束。现有方案按资源分组选择 loose / supported，再将刚度曲线平均后送入 UE 原生 Groom。发际线识别为 supported；刘海和长尾为 loose。

**缺少关系约束已证实；它是否是画面异常的唯一原因尚未通过消融验证。** 后续应分别检查扎束关系、分段支撑以及渲染发丝对导向发丝的插值，不能直接把整条马尾锁死，或只增大所有发丝刚度。

本次没有修改生产求解器来掩盖新发型的问题，原盘发继续可用。

## E · 导入与实测数据

| 发型 | 档位 | 导向发束 × 节点 | 渲染发丝 | 采样渲染点最大相邻帧位移 |
|---|---|---|---|---|
'''+ '\n'.join(table)+'''

位移为头骨局部空间、相邻 1/30 秒读回样本的峰值；包含正常运动响应，不能单独作为“鬼畜”判定。完整逐帧数据见各发型的 temporal JSON。

长发由 5 个独立资源组成，马尾由 3 个组成。粗略档减少物理导向发束和节点，保留相同的渲染发丝数量。

马尾发际线原离线头皮适配 RMS 约 0.888 cm。本次明确使用原生头皮矩阵导入，先逐项核对原始 provider 顶点、root-to-scalp 映射，再以变换结果对照独立粒子读回，最大误差约 0.000023 cm。旧离线误差保留在 source-validation.json；这是该身体的原生拟合缓存，不是假称通用离线适配已修复。

21 项单元测试通过；两个资产均由独立 UE 进程重新加载验证；四段 Game 世界录制均完成并取得模拟 GPU 读回。C++ 求解器未改动。运行链路验证与动态视觉通过是不同的结论。

[[LIFECYCLE]]

[录制凭据](recordings.json) · [选择的浏览器预设](selection.json) · [测试输出](tests.txt)

[[CONTACT]]

## F · 在 UE 中使用

下面两个版本引用同一身体资产，仅更换发型配置。可以在 Content Browser 搜索对应的 BP_VamCharacter，或将 RC_Runtime 赋给正常人物的 RuntimeConfiguration 后重新加载。已有场景中的盘发人物未被自动替换。

[[ASSETS]]
'''
(out/'draft.md').write_text(draft,encoding='utf-8')
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run([sys.executable,'-X','utf8',str(renderer),'render',str(out/'draft.md'),'--output',str(out/'report.html'),'--no-open'],check=True)
page=(out/'report.html').read_text(encoding='utf-8')
videos='';gallery='';assets=''
for key,name in names.items():
    videos+=f'<h3>{html.escape(name)}</h3><button onclick="syncPair(\'{key}\')">从头同步播放两档</button><div class="hair-grid">'
    for q,label in [('fine','精细'),('coarse','粗略')]:
        videos+=f'<figure><video id="{key}-{q}" controls preload="metadata" playsinline src="{key}/{q}.mp4" poster="{key}/motion-0000.jpg"></video><figcaption>{label}</figcaption></figure>'
    videos+='</div>'
    gallery+=f'<h3>{html.escape(name)}</h3>'
    for view,label in [('front','正面'),('right','右侧'),('rear','后面'),('left','左侧'),('oblique','斜后')]:
        gallery+=f'<details {"open" if view=="front" else ""}><summary>{label} · VaM / UE</summary><div class="hair-grid">'
        for engine,ext in [('vam','png'),('ue','jpg')]:gallery+=f'<figure><img loading="lazy" src="{key}/{engine}-{view}.{ext}"><figcaption>{engine.upper()} · 物理关闭</figcaption></figure>'
        gallery+='</div></details>'
    b=json.loads((out/key/'bundle.json').read_text())
    assets+=f'<details><summary>{html.escape(name)} · {b["identity"][:12]}</summary><p>人物：<code>{b["blueprint"]}</code></p><p>配置：<code>{b["configuration"]}</code></p><p>发型：<code>{b["hairstyle"]}</code></p><a href="{key}/bundle.json">完整资产身份</a> · <a href="{key}/source-validation.json">源数据核对</a></details>'
failure='<div class="hair-grid">'+''.join(f'<figure><img loading="lazy" src="ponytail/motion-{i:04d}.jpg"><figcaption>马尾精细档 · {i/30:.0f} 秒</figcaption></figure>' for i in [180,240])+'</div>'
contact='<p>头部接触定量审计未覆盖所有动态碰撞，不以本页截图证明完全无穿模；肩背接触和马尾束根尤其需要继续检查。</p>'
skin=out/'long/fine-skin/summary.json'
if skin.exists():
    s=json.loads(skin.read_text())['summary'];contact+=f'<p><strong>长发精细档存在渲染发丝穿入：</strong>可见发丝最大内部深度 {s["render"]["max_depth_cm"]:.3f} cm，单次采样中超过 0.2 cm 的点最多 {s["render"]["max_inside_over_02cm"]} 个；导向点对应计数为 {s["guide"]["max_inside_over_02cm"]}。这指向导向到渲染插值或渲染发丝碰撞覆盖问题，不能只查看导向点就宣称没有穿模。仅覆盖一次/秒读回与静态头部，根部前缀排除；不是全身动态碰撞验收。<a href="long/fine-skin/summary.json">详细数据</a></p>'
lifecycle=''
for key in names:
    editor=json.loads((run/key/'editor/check.json').read_text());assert editor['status']=='passed'
    shutil.copy2(run/key/'editor/check.json',out/key/'editor.json')
    lifecycle+=f'<p>{names[key]}：Editor 两档挂接及 Niagara 激活通过。<a href="{key}/editor.json">Editor 数据</a>'
    preview=run/key/'pie-preview/capture.json'
    if preview.exists():
        pie=json.loads(preview.read_text());assert pie['world']=='PIE' and pie['frames']==180
        summary={k:pie[k] for k in ['world','actor','hair','frames','simulation_dt','video_fps','orbit_camera']}
        (out/key/'pie.json').write_text(json.dumps(summary,indent=2))
        lifecycle+=f'；精细档 6 秒 PIE 短重放完成（不是四段完整录像的替代）。<a href="{key}/pie.json">PIE 数据</a>'
    else:lifecycle+='；此次新发型 PIE 尚未完成。'
    lifecycle+='</p>'
for marker,value in [('VIDEOS',videos),('GALLERY',gallery),('FAILURES',failure),('ASSETS',assets),('CONTACT',contact),('LIFECYCLE',lifecycle)]:page=page.replace('[['+marker+']]',value)
if (out/'long/contact-diagnostic.png').exists():
    page=page.replace('详细数据</a></p>','详细数据</a></p><figure><img style="width:100%" src="long/contact-diagnostic.png"><figcaption>真实 A1 头部表面与 GPU 点位投影；红星为最深内部采样点。</figcaption></figure>',1)
page=page.replace('</head>','<style>.hair-grid{display:grid;grid-template-columns:1fr 1fr;gap:14px}.hair-grid figure{margin:8px 0}.hair-grid img,.hair-grid video{width:100%;border-radius:10px}code{overflow-wrap:anywhere}button{padding:9px 16px;margin:8px 0;cursor:pointer}details{margin:12px 0}summary{cursor:pointer}@media(max-width:700px){.hair-grid{grid-template-columns:1fr}}</style></head>')
page=page.replace('</body>',"<script>function syncPair(k){const vs=['fine','coarse'].map(q=>document.getElementById(k+'-'+q));vs.forEach(v=>{v.pause();v.currentTime=0});Promise.all(vs.map(v=>v.play())).catch(()=>{});}</script></body>")
(out/'report.html').write_text(page,encoding='utf-8')
assert '[[' not in page
print(out/'report.html')
