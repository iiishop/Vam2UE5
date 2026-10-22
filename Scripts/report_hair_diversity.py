"""Publish the new-style survey from real native/UE captures, retaining failures."""
import argparse
import hashlib
import html
import json
from pathlib import Path
import shutil
import subprocess
import sys

from report_hair_dynamic_repair import capture_header,encode_capture,video_poster,archive_small_evidence,url,sha,RENDERER

ROOT=Path(__file__).resolve().parents[1]
LABELS={'short':'齐肩短发','wavy':'波浪长发','twintails':'高双马尾'}
def read(p):return json.loads(Path(p).read_text(encoding='utf-8-sig'))


def main(runs,out):
    out.mkdir(parents=True,exist_ok=True);rows=read(runs/'selection.json');results=read(runs/'results.json')
    shutil.copy2(runs/'selection.json',out/'selection.json')
    findings=read(runs/'visual-review.json') if (runs/'visual-review.json').exists() else {}
    metrics={};panels=[];performance=[];group_rows=[];proofs=[];sample_hashes=[]
    for row in rows:
        key=row['key'];label=LABELS.get(key,key);p=runs/key;dest=out/key;dest.mkdir(exist_ok=True)
        result=results.get(key,dict(status='UNVERIFIED'));metrics[key]=dict(selection=row,result=result)
        if result['status']!='MEASURED':
            panels.append(f'<h3>{html.escape(label)}：未完成</h3><pre>{html.escape(str(result))}</pre>');continue
        bundle=read(p/'bundle.json');source=read(p/'source.json');m=read(p/'pie/metrics.json');c=capture_header(p/'pie/capture.json');replay=read(p/'orbit.json')
        assert m['capture_sha256']==sha(p/'pie/capture.json') and m['replay_sha256']==sha(p/'orbit.json')
        assert c['world']=='PIE' and c['frames']==len(replay['samples'])
        assert m['head_replay_validation']['status']=='PASS' and m['head_replay_validation']['runtime_identity']==bundle['identity']
        assert read(p/'editor/check.json')['status']=='passed' and bundle['independent_reload_verified']
        sample_hashes.append(hashlib.sha256(json.dumps(replay['samples'],sort_keys=True,separators=(',',':')).encode()).hexdigest())
        v=dest/'motion.mp4';receipt=dest/'motion.json'
        if not receipt.exists():
            encoded=encode_capture(p/'pie/capture.json',v,shutil.which('ffmpeg'))
            assert encoded['status']=='ENCODED',encoded
            receipt.write_text(json.dumps(encoded,indent=2),encoding='utf8')
        assert read(receipt)['source_capture_sha256']==sha(p/'pie/capture.json')
        poster=video_poster(v,shutil.which('ffmpeg'))
        source_capture=Path(result['source_capture'])/'runtime'
        source_views=[];ue_views=[]
        for view in ['front','rear','left','right']:
            f=source_capture/(view+'.png')
            if f.exists():
                dst=dest/('vam-rest-'+view+'.png');shutil.copy2(f,dst)
                source_views.append(f'<figure><img loading="lazy" src="{url(dst,out)}"><figcaption>VaM 原始造型 · {view} · 物理关闭</figcaption></figure>')
        for index in [0,240,380,650]:
            f=p/'pie/video'/f'frame-{index:04d}.png';suffix='.jpg' if f.read_bytes()[:2]==b'\xff\xd8' else '.png';dst=dest/(f'ue-frame-{index:04d}'+suffix);shutil.copy2(f,dst)
            ue_views.append(f'<figure><img loading="lazy" src="{url(dst,out)}"><figcaption>UE PIE · {index/30:.2f}s · 精细物理开启</figcaption></figure>')
        links=[]
        for f in [p/'bundle.json',p/'editor/check.json',p/'pie/metrics.json',p/'benchmark/summary.json',p/'import/receipt.json',p/'prepare_hair_native_roots.py.log',p/'prepare_hair_render_reference.py.log',p/'prepare_hair_physics.py.log',p/'proxy-events/summary.csv',p/'proxy-events/events.json']:
            if f.exists():
                archived=archive_small_evidence(f,out);links.append(f'<a href="{url(archived,out)}">{html.escape(f.parent.name+"/"+f.name)}</a>')
        perf=read(p/'benchmark/summary.json');fine=perf['phases']['fine'];performance.append(f'| {label} | {fine["mean_ms"]:.3f} | {fine["p95_ms"]:.3f} | {perf["hair_delta_ms"]["fine"]:.3f} | {perf["off_drift_ms"]:.3f} |')
        groups=[]
        for g in m['phases']['motion']:
            gi=g['group'];policy=source['groups'][gi]['physics_policy'];kind=policy['kind'];length=g['polyline_length_ratio_to_rest'];tip=g['tip_excursion_from_phase_first_cm']['max'];proxy=g['proxy_penetration']
            head=proxy['head']['depth_cm']['max'];body=proxy['body']['depth_cm']['max']
            group_rows.append(f'| {label} G{gi} | {kind} | {tip:.3f} | {length["p95"]:.3f} / {length["max"]:.3f} | {head:.3f} / {body:.3f} |')
            groups.append(dict(group=gi,policy=policy,metrics=g,native_physics=source['groups'][gi]['native_physics'],
                               source_collision=source['groups'][gi]['source_constraints']['source_collision']))
        diagnostic=''
        diagnostic_proof=[]
        if (p/'rest-preview/capture.json').exists():
            diag=[]
            for folder,title in [('rest-preview','关闭物理'),('pie','完整物理'),('no-collision-preview','仅诊断：关闭碰撞')]:
                path=p/folder/'video/frame-0000.png'
                if path.exists():
                    header=capture_header(p/folder/'capture.json')
                    assert header['actor']==c['actor'],'Diagnostic uses a different BP'
                    diagnostic_proof.append(dict(folder=folder,header=header,capture_sha256=sha(p/folder/'capture.json'),first_frame_sha256=sha(path)))
                    dst=dest/(folder+'-first-frame.jpg');shutil.copy2(path,dst)
                    diag.append(f'<figure><img loading="lazy" src="{url(dst,out)}"><figcaption>{title} · 同一资产、姿态和相机</figcaption></figure>')
            diagnostic='<h4>预热沉降后首帧消融</h4><div class="views">'+''.join(diag)+'</div><p>两条诊断短片使用 Game 世界；正式录像为 PIE。关闭开关只用于定位问题，没有修改资产的生产设置，也不作为修复方案。</p>'
        review=findings.get(key,'尚未完成人工画面检查，不能仅凭矩阵通过宣布视觉通过。')
        panels.append(f'<h3>{html.escape(label)} · {html.escape(row["name"])}</h3><p>{html.escape(review)}</p><video class="main-video" controls preload="metadata" poster="{url(poster,out)}" src="{url(v,out)}"></video><p>同一 M 角色、同一实测头部动作、同一环绕相机。下面 VaM 静态图用于核对造型，和 UE 动态帧不是同相机的误差对照。</p><div class="views">'+''.join(ue_views)+'</div>'+diagnostic+'<details><summary>查看本次 VaM 原始造型采样与数据</summary><div class="views">'+''.join(source_views)+'</div><p>'+' · '.join(links)+'</p></details>')
        metrics[key].update(identity=bundle['identity'],blueprint=bundle['blueprint'],base_identity=bundle['base_identity'],source_sha256=sha(p/'source.json'),sample_sha256=sample_hashes[-1],head=m['head_replay_validation'],groups=groups,performance=perf,visual_review=review,diagnostics=diagnostic_proof,capture_sha256=sha(p/'pie/capture.json'))
        proofs.append(f'| {label} | {c["frames"]} | PASS | `{bundle["identity"][:24]}` |')
    assert len(set(sample_hashes))<=1,'Different actual motion inputs across survey styles'
    (out/'metrics.json').write_text(json.dumps(metrics,ensure_ascii=False,indent=2),encoding='utf8')
    draft='''---
title: 三种新发型 · 精细物理扩展验证
subtitle: 齐肩短发 / 波浪长发 / 高双马尾 · 正常 M 角色 · 实际 PIE 录像
lang: zh
---
## A 测试范围

从插件预设目录导入三种此前未参与调参的发型。沿用已修复的通用 Groom / Niagara 求解器，不按发型名称新增参数分支。每种发型在 VaM 中重新采样原始静态造型、发块和物理设置，生成独立资产，再在同一个 M 身体上运行精细档。头发自碰撞关闭。

**完成运行不等于效果通过。** 本轮保留了短发、波浪长发和双马尾的实际结果，包括造型或约束异常，不用调整单款参数隐藏通用性问题。具体结论见各视频上方。

0–8 秒沉降、8–16 秒摆头、16 秒后停止恢复；相机环绕一圈。三种发型使用相同实际头部矩阵序列。本轮没有重新采样三种发型的 VaM 动态，因此视频展示的是 UE 扩展测试，不宣称与各自 VaM 动态等价，也不代替全身走跑跳测试。

## B 真实视频与关键帧检查

[[MEDIA]]

## C 运动与接触指标

导线长度比相对于导入造型，1 表示长度相同。位移基于运动阶段首帧。代理深度来自 GPU 导线与捕获的头／身体代理，不等于真实皮肤穿模；统计排除了根部前缀。异常值必须结合实际视频判断，不因矩阵通过就标为视觉通过。

| 发型 / 发组 | 自动分类 | 发梢最大位移 cm | 长度比 P95 / Max | 头 / 身体代理深度 Max cm |
|---|---|---|---|---|
'''+ '\n'.join(group_rows)+'''

## D 性能

同一机器、单个正常 BP、1920×1080，每阶段预热 180 帧、测量 300 帧；Contact 关闭，臀腿 Jiggle 开启。整帧时间与头发渲染＋模拟增量分别列出，录像编码帧率不能当作实时帧率。

增量是单次开／关差分，关闭阶段自身也有帧时间漂移，不能把这张表当作发型性能的精确排名。

| 发型 | 整帧平均 ms | 整帧 P95 ms | 头发增量 ms | 关闭基线漂移 ms |
|---|---|---|---|---|
'''+ '\n'.join(performance)+'''

## E 身份与复现

Editor 检查普通角色的资产、附着和求解器绑定；完整动态由 PIE 实跑。每个发型保留独立 HairAsset 组合、RuntimeConfiguration 和 BP。源码求解器沿用前轮版本；本轮新增的是导入／运行证据。

| 发型 | 完整录像帧数 | 实际头骨矩阵 | 生成身份前缀 |
|---|---|---|---|
'''+ '\n'.join(proofs)+'''

[完整指标、实际 BP 路径、身份和帧时间](metrics.json)

VaM 与 UE 的材质、曝光和渲染模型仍不同。导入保持源发型造型；动态约束的充分性由本轮视频与测量暴露，不把缺点藏在通过状态里。
'''
    (out/'draft.md').write_text(draft,encoding='utf8')
    subprocess.run([sys.executable,'-X','utf8',str(RENDERER),'render',str(out/'draft.md'),'--output',str(out/'report.html'),'--no-open'],check=True)
    page=(out/'report.html').read_text(encoding='utf8').replace('[[MEDIA]]',''.join(panels))
    page=page.replace('</head>','<style>.shell{max-width:1440px}.grid>.panel{grid-column:span 12}.main-video{width:100%;max-width:1080px}.views{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}.views figure{margin:0}.views img{width:100%}.views figcaption{font-size:.8em}details{margin:20px 0 45px}@media(max-width:800px){.views{grid-template-columns:repeat(2,1fr)}}</style></head>')
    (out/'report.html').write_text(page,encoding='utf8');print(out/'report.html')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--runs',type=Path,default=ROOT/'Saved/HairDiversity');p.add_argument('--out',type=Path,default=ROOT/'Evidence/HairDiversity');a=p.parse_args();main(a.runs.resolve(),a.out.resolve())
