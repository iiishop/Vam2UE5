"""Build the self-contained A1-C1 report from retained evidence; no engine runs."""
import base64, hashlib, html, io, json, subprocess, sys
from pathlib import Path
from concurrent.futures import ThreadPoolExecutor
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
R=ROOT/'Saved/LowerBodyContact'; OUT=R/'A1-C1'
RENDER=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def esc(s):return html.escape(str(s),quote=True)
def table(headers,rows):return '<div class="scroll"><table><thead><tr>'+''.join('<th>'+esc(x)+'</th>' for x in headers)+'</tr></thead><tbody>'+''.join('<tr>'+''.join('<td>'+esc(x)+'</td>' for x in row)+'</tr>' for row in rows)+'</tbody></table></div>'
def main():
    OUT.mkdir(exist_ok=True)
    subprocess.run([sys.executable,'-X','utf8',str(RENDER),'render',str(OUT/'report.md'),'--output',str(OUT/'base.html'),'--no-open'],check=True)
    images=[]
    for stage,folder in [('A2',R/'A2-complete'),('B1',R/'B1/poses'),('B2',R/'B2/poses')]:
        for p in sorted(folder.glob('*/*.png')):
            view,mode=p.stem.split('-',1);images.append(dict(stage=stage,pose=p.parent.name,view=view,mode=mode,path=p))
    for p in sorted((R/'B2').glob('*/*.png')):
        if p.parent.name=='poses':continue
        images.append(dict(stage='B2诊断',pose=p.parent.name,view='diagnostic',mode=p.stem,path=p))
    for p in sorted((R/'audit-gpu').glob('frame-*.png')):images.append(dict(stage='A1过程',pose=p.stem,view='capture',mode='material',path=p))
    unique={}
    for item in images:
        p=item['path'];key=hashlib.sha256(p.read_bytes()).hexdigest();unique.setdefault(key,p);item['key']=key;item['original']=str(p.relative_to(ROOT)).replace('\\','/');del item['path']
    cache=OUT/'image-cache';cache.mkdir(exist_ok=True)
    def convert(entry):
        key,p=entry;out=cache/(key+'.webp')
        if not out.exists():
            with Image.open(p) as im:
                rgba=im.convert('RGBA');rgba.save(out,format='WEBP',lossless=True,exact=True,method=2)
                with Image.open(out) as saved:
                    if saved.convert('RGBA').tobytes()!=rgba.tobytes():raise ValueError('Pixel mismatch '+str(p))
        return key,'data:image/webp;base64,'+base64.b64encode(out.read_bytes()).decode('ascii')
    assets={}
    with ThreadPoolExecutor(max_workers=4) as pool:
        for i,(key,data) in enumerate(pool.map(convert,unique.items()),1):
            assets[key]=data
            if i%40==0:print('Embedded images',i,'/',len(unique),flush=True)
    def find(stage,pose,view='side',mode='gray'):
        return next(i for i in images if (i['stage'],i['pose'],i['view'],i['mode'])==(stage,pose,view,mode))
    def fig(item,caption):return '<figure><img class="zoom" loading="lazy" src="'+assets[item['key']]+'" alt="'+esc(caption)+'"><figcaption>'+esc(caption)+'</figcaption></figure>'
    def trio(pose,mode='gray'):return '<div class="trio">'+''.join(fig(find(s,pose,mode=mode),s+' · '+pose+' · 侧视 '+mode+' · UE 原始截图') for s in ['A2','B1','B2'])+'</div>'
    h=(OUT/'base.html').read_text(encoding='utf-8')
    a2=read(R/'A2-complete/metrics.json');b1=read(R/'B1/authored-targets.json');b2=read(R/'B2/summary.json');c1=read(R/'C1/runtime-audit.json')
    replacements={
      'OVERVIEW':'<h3>同一悬空坐姿：阶段成果与仍在的形态问题</h3>'+trio('suspended-seat')+'<p>上述 A2/B1 相机与局部骨骼矩阵已经逐项对照。C1 的 VaM 姿态集与此 UE 姿态集未被当作跨引擎相同 UI 角度比较。</p><details><summary>资产身份与版本范围</summary><p>A1/A2：LowerContact_20261009_v5；B1/B2/C1 所审计 UE 配置：R_251e666f091799716ce873fe。C1 contract：<code>'+esc(c1['contract_id'])+'</code>。</p><p>这是历史阶段证据汇总，不是把不同构建身份冒充同一个运行实例。</p></details>',
      'A1':table(['阶段','输出语义'],[['00-source-lbs','有来源证明的原始权重'],['01-final-weights','最终生成权重 + 参考辅助骨'],['02-static-helpers','静态结构／附着'],['03-corrective','实际激活修形'],['04-jiggle','捕获时的动态骨骼'],['05-gpu-surface','原生 GPU 读回；缺失或错帧必须明确']])+fig(images[-1],'A1 · GPU 诊断过程帧；验证读回链路，不是下半身接触验收图'),
      'A2':table(['状态','记录的验收状态','限位缩减'],[(s['id'],s['acceptance'],'是' if s['limited'] else '否') for s in a2['states']])+'<div class="trio">'+''.join(fig(find('A2',p),'A2 · '+p+' · 侧视灰模') for p in ['standing','seat-sideways','seat-prone'])+'</div>',
      'B1':table(['侧','目标','最大 delta 误差 cm','unsafe triangles'],[(p['side'],p['target'],f'{p["maximum_delta_error_cm"]:.9f}',p['unsafe_triangles']) for p in b1['poses']])+trio('seat-contact')+'<p class="warning">接触截图是失败状态的证据：Current pose invalidates contact rest volume。不能由人物接近座面推断接触求解已生效。</p>',
      'B2':'<div class="duo">'+''.join(fig(find('B2诊断','suspended-seat','diagnostic',m),'B2 坐姿 · '+m+' · 由导出数据生成的诊断图，不是 UE 截图') for m in ['fields','regional-contours'])+'</div>'+table(['姿态','分解与实际 morph 最大差 cm'],[(p['pose'],f'{p["reconstruction_max_cm"]:.9g}') for p in b2['states']]),
      'C1':table(['VaM 姿态','下半身矩阵差','重复表面最大差 cm','开关表面最大差 cm','源 LBS 下半身 RMS cm','源 LBS 最大 cm'],[(p['pose'],p['lowerbody_matrix_max_abs'],f'{max(p["off_repeat_lowerbody"]["maximum_cm"],p["on_repeat_lowerbody"]["maximum_cm"]):.6g}',f'{p["core_surface_difference"]["maximum_cm"]:.4f}',f'{p["lower_body_source_lbs_vs_vam_raw"]["rms_cm"]:.4f}',f'{p["lower_body_source_lbs_vs_vam_raw"]["maximum_cm"]:.4f}') for p in c1['poses']])+table(['骨骼','bind 中心差 cm','参考朝向差 °'],[(b['bone'],f'{b["center_error_cm"]:.6f}',f'{b["orientation_error_deg"]:.5f}') for b in c1['bind_comparison'] if b['bone'] in ['hip','pelvis','lThigh','rThigh','lShin','rShin']])
    }
    datafiles=['A2-complete/metrics.json','A2-complete/replay-check.json','A2-complete/pose-recipe.json','B1/authored-targets.json','B1/comparison-conditions.json','B1/bundle.json','B2/summary.json','B2/regional-contours.json','B2/regions.csv','B2/targets.csv','C1/runtime-audit.json','C1/contract-audit.json','C1/formula-trace.json','C1/wide-angle-existing-fit.json','C1/run-manifest.json','C1/asset-identity.json']
    downloads={name:(R/name).read_text(encoding='utf-8-sig') for name in datafiles}
    a1root=Path('I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/Saved/VamDeformationAudit')
    for p in sorted(a1root.glob('20261009-05*/replay-report.json')):downloads['A1/'+p.parent.name+'/replay-report.json']=p.read_text(encoding='utf-8-sig')
    downloads['DEFORMATION_AUDIT.md']=(ROOT/'DEFORMATION_AUDIT.md').read_text(encoding='utf-8-sig')
    downloads['image-index.json']=json.dumps(images,ensure_ascii=False,indent=2)
    replacements['ARCHIVE']='''<div id="gallery"><h3>完整图片浏览器</h3><div class="filters"><label>阶段 <select id="stage"></select></label><label>姿态 <select id="pose"></select></label><label>视角 <select id="view"></select></label><label>模式 <select id="mode"></select></label><label><input type="checkbox" id="compare" checked> A2 / B1 / B2 并排</label></div><p id="gstatus" aria-live="polite"></p><div id="gpics" class="trio"></div></div><h3>内嵌证据文件</h3><p>以下文件直接从本报告下载，离线可用。大型顶点快照与资产仍在工程原路径；图片索引保留每张原 PNG 路径和 SHA256。</p><div id="downloads"></div>'''
    replacements['ARCHIVE']+='<h3>各阶段原始报告（需要本机工程）</h3><ul>'+''.join('<li><a href="'+(R/folder/'compare.html').resolve().as_uri()+'">'+folder+'</a></li>' for folder in ['A2-complete','B1','B2','C1'])+'</ul><details><summary>完整开发记录</summary><pre>'+esc(downloads['DEFORMATION_AUDIT.md'])+'</pre></details>'
    for key,value in replacements.items():
        marker='<p>@@'+key+'@@</p>'
        if marker not in h:raise ValueError('Missing marker '+key)
        h=h.replace(marker,value)
    css='''<style>.panel,.panel.wide{grid-column:span 12}.trio{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:12px}.duo{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:12px}figure{margin:10px 0;min-width:0}figure img{display:block;width:100%;height:auto;background:#090b0e;border-radius:8px;cursor:zoom-in}figcaption{font-size:12px;color:var(--muted);margin-top:6px}.scroll{overflow:auto}code{overflow-wrap:anywhere}.warning{border-left:4px solid #d68239;padding:10px;background:var(--accentsoft)}details{margin:15px 0}summary{cursor:pointer;font-weight:650}.filters{display:flex;flex-wrap:wrap;gap:12px;align-items:center}select{padding:8px;background:var(--card);color:var(--fg);border:1px solid var(--border);border-radius:6px;max-width:230px}.nav{position:sticky;top:0;z-index:2;background:var(--card);padding:10px;border-bottom:1px solid var(--border);display:flex;gap:15px;flex-wrap:wrap}section{scroll-margin-top:75px}#downloads{display:flex;flex-wrap:wrap;gap:8px}#downloads button{font-size:12px;overflow-wrap:anywhere;text-align:left}dialog{max-width:96vw;max-height:96vh;border:0;background:var(--card);color:var(--fg);padding:12px}dialog::backdrop{background:#000c}dialog img{max-width:90vw;max-height:82vh;object-fit:contain}dialog button{float:right}@media(max-width:760px){.trio,.duo{grid-template-columns:1fr}}@media print{.nav,.filters,#downloads{display:none}.panel{break-inside:auto}.trio{grid-template-columns:repeat(3,1fr)}figure{break-inside:avoid}}</style>'''
    h=h.replace('</head>',css+'</head>')
    nav='<nav class="nav">'+''.join('<a href="#part'+str(i)+'">'+label+'</a>' for i,label in enumerate(['总结','A1','A2','B1','B2','C1','完整图库']))+'</nav>'
    h=h.replace('<body>','<body>'+nav)
    import re
    counter=iter(range(7));h=re.sub(r'<section class="panel[^\"]*"',lambda m:m.group(0)+' id="part'+str(next(counter))+'"',h)
    payload=json.dumps(dict(images=images,assets=assets,downloads=downloads),ensure_ascii=False,separators=(',',':')).replace('</','<\\/')
    js=r'''
<script id="evidence" type="application/json">PAYLOAD</script><dialog id="zoomdlg"><button class="btn" id="closezoom">关闭</button><p id="zoomcap"></p><img id="zoomimg" alt="原尺寸截图"></dialog><script>
const E=JSON.parse(document.getElementById('evidence').textContent), $=id=>document.getElementById(id);
const stages=[...new Set(E.images.map(x=>x.stage))];function opts(id,values,want){const s=$(id);s.replaceChildren(...values.map(x=>{const o=document.createElement('option');o.value=x;o.textContent=x;return o}));s.value=values.includes(want)?want:values[0]}
opts('stage',stages,'B2');function update(changed){let a=E.images.filter(x=>x.stage===$('stage').value);if(changed==='stage'){opts('pose',[...new Set(a.map(x=>x.pose))],$('pose').value||'suspended-seat')}a=a.filter(x=>x.pose===$('pose').value);if(changed==='stage'||changed==='pose')opts('view',[...new Set(a.map(x=>x.view))],$('view').value||'side');a=a.filter(x=>x.view===$('view').value);if(changed!=='mode')opts('mode',[...new Set(a.map(x=>x.mode))],$('mode').value||'gray');let selected=E.images.filter(x=>x.pose===$('pose').value&&x.view===$('view').value&&x.mode===$('mode').value&&(x.stage===$('stage').value||($('compare').checked&&['A2','B1','B2'].includes($('stage').value)&&['A2','B1','B2'].includes(x.stage))));$('gpics').replaceChildren(...selected.map(x=>{const f=document.createElement('figure'),img=document.createElement('img'),cap=document.createElement('figcaption');img.src=E.assets[x.key];img.className='zoom';img.alt=[x.stage,x.pose,x.view,x.mode].join(' · ');cap.textContent=img.alt+'｜'+x.original;f.append(img,cap);return f}));$('gstatus').textContent=`当前 ${selected.length} 张；全部 ${E.images.length} 条图像记录（相同图像数据无损去重）。B2诊断为数据投影，A1过程为链路诊断帧。`}
for(const id of ['stage','pose','view','mode'])$(id).addEventListener('change',()=>update(id));$('compare').addEventListener('change',()=>update('mode'));update('stage');document.addEventListener('click',e=>{if(e.target.matches('img.zoom')){$('zoomimg').src=e.target.src;$('zoomcap').textContent=e.target.alt;$('zoomdlg').showModal()}});$('closezoom').onclick=()=>$('zoomdlg').close();$('zoomdlg').onclick=e=>{if(e.target===$('zoomdlg'))$('zoomdlg').close()};for(const [name,text] of Object.entries(E.downloads)){const b=document.createElement('button');b.className='btn';b.textContent=name;b.onclick=()=>{const url=URL.createObjectURL(new Blob([text],{type:'text/plain;charset=utf-8'}));const a=document.createElement('a');a.href=url;a.download=name.replaceAll('/','__');a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)};$('downloads').append(b)}
</script>'''.replace('PAYLOAD',payload)
    h=h.replace('</body>',js+'</body>');out=OUT/'report.html';out.write_text(h,encoding='utf-8')
    manifest=dict(html=str(out),images=len(images),unique_images=len(unique),native_ue_screenshots=sum(x['stage'] in ['A2','B1','B2'] for x in images),lossless_images=True,embedded_evidence_files=len(downloads),bytes=out.stat().st_size)
    (OUT/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8');print(json.dumps(manifest,ensure_ascii=False),flush=True)
if __name__=='__main__':main()
