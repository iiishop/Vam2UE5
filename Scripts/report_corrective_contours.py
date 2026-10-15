"""Add per-ROI geometric silhouette comparisons to the B2 report from saved arrays."""
import json,sys
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from scipy.ndimage import binary_erosion,distance_transform_edt
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
root=Path(sys.argv[1]).resolve();suite=Path(sys.argv[2]) if len(sys.argv)>2 else root/'poses';states=json.loads((suite/'suite.json').read_text(encoding='utf-8-sig'))['states'];rows=[]
rois=['anterior_hip','lateral_glute','glute_thigh'];labels=['Anterior hip','Lateral glute','Glute-thigh transition']
for s in states:
 name=s['id'];data=np.load(root/name/'components.npz');base=data['baseline'];final=data['actual_surface'];tri=data['triangles'];w,h=s['width'],s['height'];fig,axes=plt.subplots(3,4,figsize=(14,10),layout='constrained');record={}
 for col,view in enumerate(['side','rear','front','oblique']):
  cam=next(c for c in s['cameras'] if c['view']==view);m=np.asarray(s['body_to_world']).reshape(4,4)@np.linalg.inv(np.asarray(cam['world_matrix']).reshape(4,4));f=w/(2*np.tan(np.deg2rad(cam['fov'])/2))
  def project(v):
   q=np.c_[v,np.ones(len(v))]@m;return np.c_[w/2+f*q[:,1]/q[:,0],h/2-f*q[:,2]/q[:,0]]
  bxy=project(base);fxy=project(final)
  def raster(xy):
   im=Image.new('1',(w,h));dr=ImageDraw.Draw(im)
   for t in tri:dr.polygon([tuple(x) for x in xy[t]],fill=1)
   return np.asarray(im,dtype=bool)
  bm,fm=raster(bxy),raster(fxy);be=bm&~binary_erosion(bm);fe=fm&~binary_erosion(fm);db=distance_transform_edt(~be);df=distance_transform_edt(~fe)
  for row,r in enumerate(rois):
   roi=np.maximum(data['s0_'+r],data['s1_'+r]);vs=np.r_[bxy[roi>.2],fxy[roi>.2]];lo=np.maximum(np.floor(vs.min(0)-3).astype(int),0);hi=np.minimum(np.ceil(vs.max(0)+3).astype(int),[w,h]);mask=np.zeros((h,w),bool);mask[lo[1]:hi[1],lo[0]:hi[0]]=True
   value=float(max(np.max(db[fe&mask],initial=0),np.max(df[be&mask],initial=0)));count=int(np.count_nonzero((bm^fm)&mask));item=dict(pose=name,view=view,region=r,max_boundary_distance_px=value,changed_pixels=count,roi_projected_bounds=[lo.tolist(),hi.tolist()],scope='Whole-body triangle union restricted to projected ROI bounding box; occluded regions may have no visible contour. Pixel-scale, not subpixel or anatomy validation.');rows.append(item);record[view+'/'+r]=item
   ax=axes[row,col];ax.contour(bm,levels=[.5],colors=['#009eb8'],linewidths=1);ax.contour(fm,levels=[.5],colors=['#db2670'],linewidths=1);ax.set_xlim(lo[0],hi[0]);ax.set_ylim(hi[1],lo[1]);ax.set_aspect('equal');ax.axis('off');ax.set_title(f'{labels[row]} / {view}\n{value:.2f} px max, {count} pixels changed',fontsize=10)
 fig.suptitle(name+' | regional geometric contours\nCyan: static helpers; pink: corrective. Same captured pose/shape/camera.',fontsize=12);fig.savefig(root/name/'regional-contours.png',dpi=130);plt.close(fig);(root/name/'regional-contours.json').write_text(json.dumps(record,indent=2))
(root/'regional-contours.json').write_text(json.dumps(rows,indent=2));p=root/'compare.html';text=p.read_text(encoding='utf-8')
for s in states:
 name=s['id'];old=f'<img src="{name}/contours.png">';new=old+f'<a href="{name}/regional-contours.json"><img src="{name}/regional-contours.png"></a>'
 if f'src="{name}/regional-contours.png"' not in text:text=text.replace(old,new)
p.write_text(text,encoding='utf-8');print('Regional contours:',len(rows),'measurements')
