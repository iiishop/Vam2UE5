import json,subprocess,hashlib,shutil
from pathlib import Path
import numpy as np
r=Path(__file__).resolve().parent
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
band=read(r/'runtime-band.json');result={}
for name,v in band.items():
 s=read(r/'runtime'/name/'state.json')
 edge=next(e for e in v['edges'] if e['source_edge']==[11274,16801])
 result[name]=dict(statistics=v['statistics'],probe_edge=edge,glute=s['glute_jiggle_enabled'],leg=s['leg_jiggle_enabled'])
 assert s['glute_jiggle_enabled']==('leg-only' not in name)
 assert s['leg_jiggle_enabled']==('glute-only' not in name)
 assert s['world_type']=='PIE' and s['structure_enabled'] and s['corrective_enabled']
 assert all(np.isfinite(x['max_deg']) for x in v['statistics'].values())
 assert v['statistics']['04-jiggle']['reversed_vs_static_faces']==0
assert len(result)==8
m=read(r/'slots/mapping.json');a=read(r/'slots/00-glute-input.json')['weights'];b=read(r/'slots/01-leg-transfer.json')['weights'];rows=[]
for i,s in enumerate(m['input_to_source']):
 if s in [11274,16801]:rows.append(dict(input=i,source=s,before_count=len(a[i]),before=a[i],after=b[i]))
assert len(a[11274])==8 and a[11274]==b[11274]
assert len(a[16801])==7 and abs(dict(b[16801])[126]-.6075196355581284)<1e-8
(r/'slot-proof.json').write_text(json.dumps(dict(identity=m['identity'],source=m['source'],rows=rows,limitation='Isolated builder replay: final Glute profile duplicated; only early input/transfer data used. Optional region_weights Python reflection failed after weight files were written. Mapping then read successfully in separate process; both logs retained.'),indent=2))
ff='C:/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-9.0.1-full_build/bin/ffprobe.exe'
videos={}
for f in ['both','leg-only','glute-only','comparison']:
 x=json.loads(subprocess.check_output([ff,'-v','error','-count_frames','-show_entries','stream=nb_read_frames,duration,r_frame_rate,width,height','-of','json',str(r/(f+'.mp4'))]))['streams'][0]
 assert x['nb_read_frames']=='180' and float(x['duration'])==6 and x['r_frame_rate']=='30/1'
 videos[f]=x
for name in ['seated-both-moving','seated-leg-only','seated-glute-only']:
 samples=read(r/'runtime'/name/'video.json')['samples'];assert len(samples)==180
 assert len(list((r/'runtime'/name/'video').glob('frame-*.png')))==180
 assert all(s['root_z_cm']==0 for s in samples[90:])
 helper=np.array([next(n for n in s['helper_matrices'] if n['bone']=='R_Thigh_Hamstrings')['matrix'] for s in samples])
 videos[name]=dict(root_motion_cm=[min(s['root_z_cm'] for s in samples),max(s['root_z_cm'] for s in samples)],hamstring_component_translation_range_cm=np.ptp(helper[:,12:15],axis=0).tolist())
log=(r/'runtime/runtime.log').read_text(errors='replace');assert 'Result={Success} Name={ContactRuntime}' in log
(r/'validation.json').write_text(json.dumps(dict(asset=read(r/'runtime/asset-identity.json'),compile='Succeeded',runtime_automation='Success',cases=result,videos=videos,production_modified=False,wrinkle_fixed=False),indent=2))
print(json.dumps({k:v['statistics']['04-jiggle'] for k,v in result.items()},indent=2))
