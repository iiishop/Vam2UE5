"""Publish verified M runtime footage and separately sourced animation research."""
import argparse,json,shutil,subprocess,html
from pathlib import Path
from PIL import Image,ImageDraw
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
p=argparse.ArgumentParser();p.add_argument('run',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(parents=True,exist_ok=True)
analysis=read(a.run/'operator-analysis.json');sections=[]
for action in analysis['actions']:
    name='M-'+action['action'];src=a.run/'M'/name;old=Path('Saved/KneeDeepRepair/pie-final/M')/name;dst=a.out/name;dst.mkdir(exist_ok=True)
    gates=action['pose_camera_gates'];assert max(x['camera_max'] for x in gates)<1e-6 and max(x['knee_max_deg'] for x in gates)<1e-4
    # UE PNG exports may contain BMP pixels; decode before passing to ffmpeg.
    for label,folder in [('before',old),('after',src)]:
        clean=a.run/'encode'/name/label;clean.mkdir(parents=True,exist_ok=True)
        for f in sorted((folder/'video').glob('frame-*.png')):
            if not (clean/f.name).exists():Image.open(f).save(clean/f.name,compress_level=1)
        video=dst/(label+'.mp4')
        if not video.exists():subprocess.run(['ffmpeg','-v','error','-y','-framerate','30','-i',str(clean/'frame-%04d.png'),'-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(video)],check=True)
    pair=dst/'comparison.mp4'
    if not pair.exists():subprocess.run(['ffmpeg','-v','error','-y','-filter_complex_threads','1','-i',str(dst/'before.mp4'),'-i',str(dst/'after.mp4'),'-filter_complex','hstack=inputs=2','-c:v','libx264','-threads','2','-crf','18','-pix_fmt','yuv420p','-movflags','+faststart',str(pair)],check=True)
    sheet=Image.new('RGB',(1200,680),'#edf1f4');draw=ImageDraw.Draw(sheet)
    for row,folder in enumerate([old,src]):
        for col,frame in enumerate([90,135,165]):
            im=Image.open(folder/'video'/f'frame-{frame:04d}.png').crop((130,180,680,720)).resize((400,310));sheet.paste(im,(col*400,row*340+25));draw.text((col*400+8,row*340+4),f'{"BEFORE" if row==0 else "AFTER"} | {frame/30:.1f}s',fill='black')
    sheet.save(dst/'comparison.png')
    close=Image.new('RGB',(920,380),'#edf1f4')
    for col,folder in enumerate([old,src]):close.paste(Image.open(folder/'deep-side.png').crop((420,430,650,620)).resize((460,380)),(col*460,0))
    close.save(dst/'knee-closeup.png')
    vam=Path('Saved/LowerBodyContact/C1.5/knee-M-20261022/runtime')/(action['action']+'-0165-motion-gray-runtime-surface.png')
    shutil.copy2(vam,dst/'vam-gray.png')
    for view in ['side','rear','oblique']:Image.open(src/f'deep-{view}.png').save(dst/(view+'.png'))
    peak=action['frames'][-1]
    edges=''.join(f'<tr><td>{e["triangles"]}</td><td>{e["before_deg"]:.2f}°</td><td>{e["after_deg"]:.2f}°</td></tr>' for e in peak['edges'])
    sections.append(f'<section><h2>{html.escape(name)}</h2><p>视频：左为原 M，右为本次 M。相同相机、灯光、实际骨骼姿态；Jiggle 开启，Contact 关闭。155° 属诊断深屈曲输入，生产关节限位未改。</p><video controls preload="metadata" src="{name}/comparison.mp4"></video><img src="{name}/comparison.png"><p>膝部局部放大：左原 M，右本次。仅裁剪缩放。</p><img src="{name}/knee-closeup.png"><table><tr><th>三角形对</th><th>修复前夹角</th><th>修复后夹角</th></tr>{edges}</table><details><summary>真实 UE 三视图</summary>'+''.join(f'<img src="{name}/{view}.png">' for view in ['side','rear','oblique'])+f'</details><details><summary>同姿态实际 VaM 表面</summary><p>实际 VaM smoothedVertsBuffer 灰模捕获；材质/渲染器不同，不用亮度差代替几何误差。对应源顶点的数值对照见 JSON。</p><img src="{name}/vam-gray.png"></details></section>')
shutil.copy2(a.run/'operator-analysis.json',a.out/'operator-analysis.json');shutil.copy2(a.run/'identity.json',a.out/'identity.json');shutil.copy2(a.run/'validation.json',a.out/'validation.json')
research=read(Path('Config/MKneeOperator/zzz-research.json'));shutil.copy2('Config/MKneeOperator/zzz-research.json',a.out/'zzz-research.json')
sources=''.join(f'<li><a href="{html.escape(s["url"])}">{html.escape(s["type"])}</a>：{html.escape(s["confirmed"])}<br><small>{html.escape(s["scope"])}</small></li>' for s in research['sources'])
body='''<section><h2>根因与修复</h2><p>Knee corrective 在参考辅助骨算子下生成，原 M 的肌肉缩放又对这份修形位移进行变换，造成局部二次拉伸。现在保留 M 基础肌群形变和 Jiggle，只补偿 Knee corrective 的方向变换差，并在原生 Optimus 图中叠加、重建法线。接触 animated target 使用相同规则。</p><pre>P = B_M(P0 + Δknee + Δother) + (B_ref − B_M)Δknee</pre><p>CPU 计算方向差，GPU 应用最终位移；不是新增 GPU 物理求解器。Contact 关闭仍生效。保留原始分层、补偿后分层和真实 GPU 回读。</p><p><b>范围限制：</b>这修复 M 与膝部修形的组合错误，不等于所有区域已经精确还原 VaM。VaM 的表面平滑与隐藏 Morph 输入差异仍分别记录；没有通过统一降低 M 振幅或关闭 Jiggle 掩盖。</p></section>'''
body+=''.join(sections)+f'<section><h2>《绝区零》：公开事实与可借鉴方法</h2><ul>{sources}</ul><h3>本项目的工程推断，不是其未公开内部实现</h3><ul>'+''.join('<li>'+html.escape(x)+'</li>' for x in research['engineering_inference'])+'</ul></section>'
body+='<section><h2>验证记录</h2><p id="validation-summary">详细验收待最终数据核对。</p><a href="operator-analysis.json">完整数值、逐帧相机/姿态门槛及 GPU 回读</a> · <a href="identity.json">生成与验证身份</a> · <a href="validation.json">运行记录</a></section>'
(a.out/'report.html').write_text('<!doctype html><html lang="zh"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>M 膝部算子一致性修复</title><style>body{max-width:1450px;margin:30px auto;padding:20px;background:#eef2f5;color:#152a3b;font:17px/1.7 system-ui}section{background:white;padding:24px;margin:20px 0;border-radius:12px}video,img{max-width:100%;display:block;margin:12px auto}details img{max-width:768px}table{border-collapse:collapse;width:100%}td,th{padding:10px;border-bottom:1px solid #ccd}pre{overflow:auto;background:#edf1f4;padding:15px}small{color:#596774}</style><h1>M 膝部修形修复 · UE / VaM 对照</h1>'+body+'</html>',encoding='utf-8')
print(a.out/'report.html')
