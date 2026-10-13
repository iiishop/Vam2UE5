import json,subprocess,shutil,gzip,hashlib,re,struct
from pathlib import Path
r=Path(__file__).resolve().parent;out=r.parents[2]/'Evidence/LegProduction';out.mkdir(exist_ok=True)
renderer=Path('C:/Users/Administrator/.codex/plugins/cache/created-by-me-remote/answer-me-with-html-chatgpt/1.0.0/skills/answer-me-with-html/scripts/render.py')
subprocess.run(['F:/Program/Python311/python.exe','-X','utf8',str(renderer),'render',str(r/'report.md'),'--output',str(r/'report.html'),'--no-open'],check=True)
files=['report.md','report.html','motion-analysis.json','dynamic-seam.json','coverage-comparison.json','static-regression.json','main-validation.json','installation.json','compile-v3-retry.log','host-source-parity-v3.json','analyze_production.py','coverage_production.py','dynamic_seam.py','static_regression.py','verify_main.py','encode.py','publish.py','seam-before.json','seam-after.json']
for n in files:shutil.copy2(r/n,out/n)
for n in ['bundle.json','tests.json','authored-targets.json','recipe.json','family.json','motion-recipe.json','editor-recipe.json','seated-recipe.json','pie-static-recipe.json','test-command.json','run.py','tests.py']:shutil.copy2(r/'v3'/n,out/n)
for n in ['build.log','reload.log','verify.log','tests.log']:
 with gzip.open(out/(n+'.gz'),'wb') as f:f.write((r/'v3'/n).read_bytes())
shutil.copytree(r/'media',out/'media',dirs_exist_ok=True)
for group in ['before-v3','after-v3','editor-before','main-editor','main-pie']:
 d=r/group;dest=out/group;dest.mkdir(exist_ok=True)
 assert 'Result={Success} Name={ContactRuntime}' in (d/'runtime.log').read_text(errors='replace')
 for n in ['command.json','asset-identity.json','process.json','suite.json']:shutil.copy2(d/n,dest/n)
 with gzip.open(dest/'runtime.log.gz','wb') as f:f.write((d/'runtime.log').read_bytes())
 for case in d.glob('*/state.json'):
  cd=dest/case.parent.name;cd.mkdir(exist_ok=True);shutil.copy2(case,cd/'state.json')
  pictures=['rear-gray.png'] if group in ['editor-before','main-editor'] else ['oblique-material.png','side-world-normal.png','side-wireframe.png'] if case.parent.name=='seated-stop' else ['oblique-material.png'] if case.parent.name=='seated-jiggle' else []
  for n in pictures:shutil.copy2(case.parent/n,cd/n)
  if (case.parent/'video.json').exists():
   with gzip.open(cd/'video.json.gz','wb') as f:f.write((case.parent/'video.json').read_bytes())
media='<section id="motion-evidence" class="evidence"><h2>真实 UE 同条件动画</h2><p><strong>左：旧生产角色；右：新正式角色。</strong>下方每个视频已同步并排。无插帧、无慢放。建议先看坐姿急停局部。</p>'
labels={'seated-stop':'坐姿急停：重点看臀腿交界','flexion':'屈伸','step':'迈步关节重放','hard-stop':'站立急停'}
for case,label in labels.items():
 media+=f'<h3>{label}</h3><video controls preload="metadata" src="media/{case}-comparison.mp4"></video><details><summary>展开同步局部放大</summary><video controls preload="metadata" src="media/{case}-detail.mp4"></video></details>'
media+='<h2>坐姿 Jiggle 开启：真实静帧</h2><p>左旧右新；主材质、法线、线框使用同一姿态与光照。</p>'
for n,label in [('oblique-material.png','材质'),('side-world-normal.png','World Normal'),('side-wireframe.png','线框')]:
 media+=f'<h3>{label}</h3><div class="pair"><figure><img src="before-v3/seated-stop/{n}"><figcaption>旧版</figcaption></figure><figure><img src="after-v3/seated-stop/{n}"><figcaption>正式新版</figcaption></figure></div>'
media+='<h2>静态臀部形态回归</h2><p>左：旧版 Editor；右：主工程新版 Editor。Jiggle 关闭；辅助骨、Corrective 保留。</p>'
for case in ['standing','suspended-seat','abducted','knee-120']:
 media+=f'<h3>{case}</h3><div class="pair"><figure><img src="editor-before/{case}/rear-gray.png"><figcaption>旧版</figcaption></figure><figure><img src="main-editor/{case}/rear-gray.png"><figcaption>正式新版</figcaption></figure></div>'
media+='</section>'
style='<style>.evidence{max-width:1300px;margin:24px auto;padding:24px;background:#eaf0f6;color:#172a40;border-radius:16px}.evidence video{display:block;width:100%;max-height:780px;background:#000;margin:12px 0}.pair{display:grid;grid-template-columns:1fr 1fr;gap:12px}.pair figure{margin:0}.pair img{width:100%;display:block}.evidence details{padding:8px;background:#dbe5ed}.evidence h2{margin-top:32px}</style>'
html=(r/'report.html').read_text(encoding='utf-8').replace('</head>',style+'</head>').replace('</main>',media+'</main>')
for p in [r/'report.html',out/'report.html']:p.write_text(html,encoding='utf-8')
# Local Saved report uses the same copied comparison subdirectories already present.
for n in ['authored-targets.json','tests.json','bundle.json']:shutil.copy2(r/'v3'/n,r/n)
for s in re.findall(r'(?:href|src)="([^"]+)"',media):assert (out/s).is_file(),s
validation={'code_commit':'8625586','code_push':'origin/fix/allbody succeeded','tests':15,'authored_targets':20,'main_editor_pie_matched_poses':4,'main_pie_seated_jiggle':True,'raw_video_frames':1680,'comparison_videos':4,'contact_scope':'Disabled for leg isolation; no historical contact acceptance claimed.'}
(out/'validation.json').write_text(json.dumps(validation,indent=2))
# UE's exported buffer includes trailing bytes after PNG IEND. Strip only this
# unused tail from archive copies, retaining every PNG chunk and encoded pixel.
png_receipts={}
for p in out.rglob('*.png'):
 data=p.read_bytes();i=8
 while i+12<=len(data):
  n=struct.unpack('>I',data[i:i+4])[0];kind=data[i+4:i+8];i+=12+n
  if kind==b'IEND':break
 assert kind==b'IEND' and i<=len(data)
 png_receipts[str(p.relative_to(out))]={'raw_sha256':hashlib.sha256(data).hexdigest(),'trailing_bytes_removed':len(data)-i,'pixel_chunks_unchanged':True};p.write_bytes(data[:i])
(out/'png-storage.json').write_text(json.dumps(png_receipts,indent=2))
hashes={str(p.relative_to(out)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file() and p.name!='manifest.json'}
(out/'manifest.json').write_text(json.dumps({'source_root':str(r),'files':hashes,'raw_A1':'Retained at audit_bundle paths in state.json; original PNG sequences remain in source_root.','replay':'Run analysis scripts in source_root. Evidence archives compressed logs/video states and selected screenshots.'},indent=2))
print(out/'report.html')
