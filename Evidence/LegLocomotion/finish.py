import json,subprocess,gzip,shutil,hashlib
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
r=Path(__file__).resolve().parent
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
ff=Path('C:/Users/Administrator/AppData/Local/Microsoft/WinGet/Packages/Gyan.FFmpeg_Microsoft.Winget.Source_8wekyb3d8bbwe/ffmpeg-9.0.1-full_build/bin')
media=r/'media';media.mkdir(exist_ok=True)
data={k:read(r/'final'/k/'locomotion.json') for k in ['jiggle-on','jiggle-off']}
state=read(r/'final/jiggle-on/state.json');bundle=Path(state['audit_bundle']);inputs=read(bundle/'04-jiggle/inputs.json')
names=[b['name'] for b in inputs['bones']]
mat={k:np.array([s['bones'] for s in v['samples']]).reshape(480,-1,4,4) for k,v in data.items()}
primary=[i for i,n in enumerate(names) if not any(x in n for x in ['_Breast','_Glute_','_Thigh_','_Calf_'])]
helpers=[i for i,n in enumerate(names) if 'Glute_' in n or 'Thigh_' in n or 'Calf_' in n]
result={'asset':read(r/'final/asset-identity.json'),'frames':480,'seconds':16,'world':state['world_type'],'glute_enabled':data['jiggle-on']['glute_jiggle'],'leg_enabled':data['jiggle-on']['leg_jiggle'],'finite_bones':all(np.isfinite(v).all() for v in mat.values()),'same_camera':all(np.max(abs(np.array(a['camera'])-b['camera']))<1e-3 for a,b in zip(data['jiggle-on']['samples'],data['jiggle-off']['samples'])),'same_trajectory':all(np.max(abs(np.array(a['actor'])-b['actor']))<1e-3 for a,b in zip(data['jiggle-on']['samples'],data['jiggle-off']['samples'])),'primary_bones_max_difference':float(abs(mat['jiggle-on'][:,primary]-mat['jiggle-off'][:,primary]).max()),'distance_cm':data['jiggle-on']['samples'][-1]['distance_cm'],'walk_cm_s':data['jiggle-on']['walk_speed_cm_s'],'run_cm_s':data['jiggle-on']['run_speed_cm_s']}
t=np.array([s['time'] for s in data['jiggle-on']['samples']]); delta=np.linalg.norm(mat['jiggle-on'][:,:,3,:3]-mat['jiggle-off'][:,:,3,:3],axis=-1)
result['groups']={}
for label,token in [('glute','Glute_'),('thigh','Thigh_'),('calf','Calf_')]:
 ids=[i for i,n in enumerate(names) if token in n]; assert ids,(label,names)
 x=delta[:,ids];peak=np.unravel_index(np.argmax(x),x.shape)
 result['groups'][label]={'helper_count':len(ids),'max_output_offset_cm':float(x.max()),'peak_time':float(t[peak[0]]),'peak_bone':names[ids[peak[1]]],'max_offset_after_13s_cm':float(x[t>=13].max()),'max_offset_final_second_cm':float(x[t>=15].max()),'walking_max_cm':float(x[(t>=2)&(t<5)].max()),'running_max_cm':float(x[(t>=6.5)&(t<10.5)].max())}
result['solver_limit_corrections']={k:max(g['limits'] for s in data['jiggle-on']['samples'] for g in s[k]) for k in ['leg','glute']}
result['retarget']={k:data['jiggle-on'][k] for k in ['walk','run','walk_play_rate','run_play_rate','retarget_clamp_count','max_clamp_degrees']}
result['primary_position_max_cm']=float(np.linalg.norm(mat['jiggle-on'][:,primary,3,:3]-mat['jiggle-off'][:,primary,3,:3],axis=-1).max())
result['primary_rotation_matrix_element_max']=float(abs(mat['jiggle-on'][:,primary,:3,:3]-mat['jiggle-off'][:,primary,:3,:3]).max())
result['comparison_scope']='Independent sequential PIE clips. Not bit-identical: report primary pose mismatch. Visual comparison tolerance 0.05 cm translation and 0.002 rotation-matrix elements; do not infer sub-mm surface differences from this comparison.'
(r/'analysis.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
assert result['finite_bones'] and result['same_camera'] and result['same_trajectory']
assert result['primary_position_max_cm']<.05 and result['primary_rotation_matrix_element_max']<.002
def run(args):
 with (media/'encode.log').open('a') as f:subprocess.run([str(ff/'ffmpeg.exe'),'-y',*args],stdout=f,stderr=subprocess.STDOUT,check=True)
for k in data:
 frames=r/'final'/k/'video';assert len(list(frames.glob('frame-*.png')))==480
 run(['-framerate','30','-i',str(frames/'frame-%04d.png'),'-c:v','libx264','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(media/(k+'.mp4'))])
run(['-i',str(media/'jiggle-off.mp4'),'-i',str(media/'jiggle-on.mp4'),'-filter_complex','[0:v]crop=420:450:420:220[a];[1:v]crop=420:450:420:220[b];[a][b]hstack=inputs=2[v]','-map','[v]','-c:v','libx264','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(media/'detail-off-on.mp4')])
run(['-i',str(media/'detail-off-on.mp4'),'-vf','setpts=2*PTS','-an','-c:v','libx264','-crf','17','-pix_fmt','yuv420p','-movflags','+faststart',str(media/'detail-half-speed.mp4')])
checks={}
for p in media.glob('*.mp4'):
 checks[p.name]=json.loads(subprocess.check_output([str(ff/'ffprobe.exe'),'-v','error','-count_frames','-show_entries','stream=nb_read_frames,duration,width,height','-of','json',str(p)]))['streams'][0]
(media/'validation.json').write_text(json.dumps(checks,indent=2))
grid=Image.new('RGB',(1280,4*390),(22,25,30));d=ImageDraw.Draw(grid)
for row,f in enumerate([90,240,319,345]):
 for col,k in enumerate(['jiggle-off','jiggle-on']):
  im=Image.open(r/'final'/k/'video'/f'frame-{f:04d}.png').convert('RGB');im=im.resize((640,360));grid.paste(im,(col*640,row*390+30));d.text((col*640+15,row*390+8),f'{k} / {f/30:.2f}s',fill='white')
grid.save(r/'contact-sheet.jpg',quality=94)
