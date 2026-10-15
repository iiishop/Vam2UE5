from pathlib import Path
import numpy as np,json,re
r=Path(__file__).resolve().parents[1]/'Saved/ContactPerformance/broadphase';out={}
phases=['uniform_nipple_press','press_left','central_sphere','off_nipple_sphere','press_right','translated_press','rotated_press']
for d in r.iterdir():
 if not (d/'summary.json').exists():continue
 data=json.loads((d/'summary.json').read_text());data['mean_press_ms']=sum(float(data['phases'][x]) for x in phases)/len(phases);data['surface_diff']=[]
 for f in sorted((r/'baseline').glob('gpu-*.bin')):
  if not (d/f.name).exists():continue
  a=np.fromfile(f,dtype=np.float32).reshape(-1,3);b=np.fromfile(d/f.name,dtype=np.float32).reshape(-1,3);diff=np.linalg.norm(a.astype(float)-b.astype(float),axis=1)
  data['surface_diff'].append({'snapshot':f.name,'max_cm':float(diff.max()),'rms_cm':float(np.sqrt(np.mean(diff*diff))),'identical':bool(np.array_equal(a,b))})
 s=(d/'runtime.log').read_text(errors='replace');data['solver_detail']=re.findall(r'Solver detail ms native ([\d.]+) volume ([\d.]+) post-contact ([\d.]+)',s);data['trajectory']=re.findall(r'CONTACT_TRAJECTORY .*',s)
 out[d.name]=data;print(d.name,round(data['mean_press_ms'],3),data['tests'],'max_cm',max([x['max_cm'] for x in data['surface_diff']],default=0),data['trajectory'])
if (r/'ab/gpu-04.bin').exists():
 a=np.fromfile(r/'ab/gpu-04.bin',dtype=np.float32).reshape(-1,3)
 pairs=[]
 for i in [5,6,7]:
  f=r/('ab/gpu-%02d.bin'%i)
  if f.exists():
   b=np.fromfile(f,dtype=np.float32).reshape(-1,3);v=np.linalg.norm(a.astype(float)-b.astype(float),axis=1)
   pairs.append({'snapshot':f.name,'max_cm':float(v.max()),'identical':bool(np.array_equal(a,b))})
 if 'ab' in out:out['ab']['alternating_surface_diff']=pairs
 print('ABBA surface:',pairs)
(r/'results.json').write_text(json.dumps(out,indent=2))
