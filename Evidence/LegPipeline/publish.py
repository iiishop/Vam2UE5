import json,subprocess,shutil,gzip,hashlib,re
from pathlib import Path
r=Path(__file__).resolve().parent;out=r.parents[2]/'Evidence/LegPipeline';out.mkdir(exist_ok=False)
ff='C:/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-9.0.1-full_build/bin/ffprobe.exe'
validation={}
for p in r.glob('*.mp4'):
 d=json.loads(subprocess.check_output([ff,'-v','error','-count_frames','-show_entries','stream=nb_read_frames,duration,r_frame_rate','-of','json',str(p)]))['streams'][0];assert d['nb_read_frames']=='180' and d['r_frame_rate']=='30/1' and float(d['duration'])==6;validation[p.name]=d;shutil.copy2(p,out/p.name)
media='<section style="max-width:1300px;margin:24px auto;padding:24px;background:#eaf0f6;color:#172a40;border-radius:16px"><h2>真实 UE 录像：预载因果对照</h2><p>左：当前生产模型。右：仅诊断用姿态预载探针，改变了静态下垂，不是正式修复。</p><video controls preload="metadata" src="preload-comparison.mp4" style="width:100%"></video><h2>六组完整运动</h2><div style="display:grid;grid-template-columns:repeat(3,1fr);gap:16px">'
for name,label in [('standing-up','站立：上下'),('standing-front','站立：前后'),('seated-up','坐姿：上下'),('seated-front','坐姿：前后'),('seated-up-pose-preload','预载探针：坐姿上下'),('seated-front-pose-preload','预载探针：坐姿前后')]:
 media+=f'<div><h3>{label}</h3><video controls preload="metadata" src="{name}.mp4" style="width:100%"></video></div>'
media+='</div><p>2 cm / 1 Hz；前三秒运动，后三秒停止。录制帧率 30 fps，不是性能基准。<a href="analysis.json">逐节点与表面统计</a> · <a href="coverage.json">权重覆盖</a></p></section>'
p=r/'report.html';html=p.read_text(encoding='utf-8').replace('</main>',media+'</main>');p.write_text(html,encoding='utf-8')
for name in ['report.md','report.html','analysis.json','coverage.json','recipe.json','probe-recipe.json','bundle.json','compile.log','compile-probe.log','coverage.py','analyze.py','publish.py']:shutil.copy2(r/name,out/name)
for group in ['runtime','probe']:
 log=(r/group/'runtime.log').read_text(errors='replace');assert 'Result={Success} Name={ContactRuntime}' in log
 dest=out/group;dest.mkdir()
 for name in ['command.json','asset-identity.json','process.json','suite.json']:shutil.copy2(r/group/name,dest/name)
 with gzip.open(dest/'runtime.log.gz','wb') as f:f.write((r/group/'runtime.log').read_bytes())
 for case in (r/group).glob('*/video.json'):
  cd=dest/case.parent.name;cd.mkdir();samples=json.loads(case.read_text())['samples'];assert len(samples)==180
  assert len(list((case.parent/'video').glob('frame-*.png')))==180
  with gzip.open(cd/'video.json.gz','wb') as f:f.write(case.read_bytes())
  shutil.copy2(case.parent/'state.json',cd/'state.json');shutil.copy2(case.parent/'video/frame-0040.png',cd/'frame-0040.png')
(r/'validation.json').write_text(json.dumps(dict(videos=validation,PIE_batches=2,cases=6,raw_frame_count=1080,production_changed=False),indent=2));shutil.copy2(r/'validation.json',out/'validation.json')
hashes={str(p.relative_to(out)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}
(out/'manifest.json').write_text(json.dumps(dict(source_root=str(r),files=hashes,raw_data='Raw PNG and A1 bundles retained at source_root and paths in state.json. Compressed video.json records retain the exact per-frame pipeline.',note='Baseline first batch used earlier harness protocol text; motion direction is established by recipe.video_direction and recorded root_offset_cm, not legacy vertical wording.'),indent=2))
for s in re.findall(r'(?:href|src)="([^"]+)"',media):assert (out/s).exists()
assert html.count('<video')==7
print(out)
