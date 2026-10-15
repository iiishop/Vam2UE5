import json,shutil,hashlib,re
from pathlib import Path
r=Path(__file__).resolve().parent;repo=r.parents[2];out=repo/'Evidence/ThighJiggleFold'
assert repo.name=='VamResourceBrowser'
media='''<section style="max-width:1400px;margin:24px auto;padding:24px;background:#eaf0f6;color:#142435;border-radius:16px"><h2>真实 UE 运动对照</h2><p>同步局部：左＝臀腿开启，中＝仅腿部，右＝仅臀部。前 3 秒上下运动，后 3 秒停止回弹。</p><video controls preload="metadata" poster="comparison.png" src="comparison.mp4" style="width:100%"></video><div style="display:grid;grid-template-columns:repeat(3,1fr);gap:12px">'''
for f,label in [('both','臀腿开启'),('leg-only','仅腿部'),('glute-only','仅臀部')]:
 media+=f'<div><h3>{label} · 完整画面</h3><video controls preload="metadata" src="{f}.mp4" style="width:100%"></video></div>'
media+='</div><h2>相同条件截图</h2><div style="display:grid;grid-template-columns:repeat(2,1fr);gap:12px">'
for case,label in [('seated-both-still','静止，臀腿开启'),('seated-both-moving','运动，臀腿开启'),('seated-leg-only','运动，仅腿部'),('seated-glute-only','运动，仅臀部'),('abducted-both-moving','外展，臀腿开启'),('abducted-glute-only','外展，仅臀部')]:
 media+=f'<figure><img loading="lazy" src="runtime/{case}/oblique-material.png" style="width:100%"><figcaption>{label}</figcaption></figure>'
media+='</div><p><a href="validation.json">PIE 与录像验证</a> · <a href="slot-proof.json">8 槽位生成证据</a> · <a href="attribution.json">分层归因</a> · <a href="runtime-band.json">8 个状态的全部角度</a></p></section>'
p=r/'report.html';html=p.read_text(encoding='utf-8');html=html.replace('</main>',media+'</main>');p.write_text(html,encoding='utf-8')
out.mkdir(parents=True,exist_ok=False)
for f in ['report.md','report.html','comparison.png','comparison.mp4','both.mp4','leg-only.mp4','glute-only.mp4','recipe.json','bundle.json','validation.json','slot-proof.json','attribution.json','runtime-band.json','analyze.py','inspect_slots.py','inspect_map.py','summarize.py','publish.py','compile.log','slots.log','slots-map.log']:
 shutil.copy2(r/f,out/f)
for f in ['runtime.log','command.json','asset-identity.json','process.json','suite.json']:
 (out/'runtime').mkdir(exist_ok=True);shutil.copy2(r/'runtime'/f,out/'runtime'/f)
for case in (r/'runtime').iterdir():
 if not case.is_dir():continue
 dest=out/'runtime'/case.name;dest.mkdir(exist_ok=True)
 for name in ['state.json','video.json','oblique-material.png','oblique-gray.png']:
  if (case/name).exists():shutil.copy2(case/name,dest/name)
hashes={}
for f in sorted(out.rglob('*')):
 if f.is_file():hashes[str(f.relative_to(out)).replace('\\','/')]=hashlib.sha256(f.read_bytes()).hexdigest()
(out/'manifest.json').write_text(json.dumps(dict(source_root=str(r),files=hashes,raw_frames='540 source PNG frames retained under source_root/runtime/*/video',scope='Diagnostic capture, not a production fix. A1 audit_bundle paths in per-case state.json retain complete layered matrices and surfaces.'),indent=2))
for src in re.findall(r'(?:src|href)="([^"]+)"',media):assert (out/src).exists(),src
print(out)
