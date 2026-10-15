"""Plot quantitative A1 evidence. These plots are not UE screenshots."""
import argparse,json
from pathlib import Path
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from audit_lower_body_boundaries import read,bundle,data
p=argparse.ArgumentParser();p.add_argument('before',type=Path);p.add_argument('after',type=Path);p.add_argument('pairs',type=Path);p.add_argument('out',type=Path);a=p.parse_args();a.out.mkdir(exist_ok=True,parents=True);pairs=read(a.pairs)['pairs'];fig,axes=plt.subplots(1,2,figsize=(12,6),constrained_layout=True)
for ax,root,label in zip(axes,[a.before,a.after],['Before','After']):
 b=bundle(root,'abducted');sid,s=data(b,'00-source-lbs');fid,f=data(b,'03-corrective');src={int(i):x for i,x in zip(sid,s) if i>=0};final={int(i):x for i,x in zip(fid,f) if i>=0};pair=[(l,r) for l,r in pairs if l in final and r in final and l in src and r in src];v=np.array([final[l] for l,r in pair]);e=np.array([np.linalg.norm((final[l]-src[l])-(final[r]-src[r])*[1,-1,1]) for l,r in pair]);q=ax.scatter(v[:,1],v[:,2],c=e,s=12,cmap='inferno',vmin=0,vmax=5);ax.set_title(label+' | extra mirror error');ax.set_xlabel('Component Y (cm)');ax.set_ylabel('Component Z (cm)');ax.set_aspect('equal');ax.grid(alpha=.2)
fig.colorbar(q,ax=axes,label='Extra mirror error vs source LBS (cm)');fig.suptitle('Abducted: captured UE A1 points, source-ID pairing\nNumeric diagnostic plot; not an engine screenshot');fig.savefig(a.out/'mirror-heatmap.png',dpi=150);plt.close(fig)
fig,axes=plt.subplots(1,2,figsize=(12,5),constrained_layout=True)
for root,label,style in [(a.before,'Before','--'),(a.after,'After','-')]:
 b=bundle(root,'abducted');sid,s=data(b,'00-source-lbs');fid,f=data(b,'03-corrective');src={int(i):x for i,x in zip(sid,s) if i>=0};final={int(i):x for i,x in zip(fid,f) if i>=0}
 for ax,side in zip(axes,[0,1]):
  keys=[p[side] for p in pairs if p[side] in final and p[side] in src];z=np.array([src[k][2] for k in keys]);center=np.percentile(z,35);keys=[k for k in keys if abs(src[k][2]-center)<1.0];v=np.array([final[k] for k in keys]);order=np.argsort(v[:,1]);ax.plot(v[order,1],v[order,0],style,marker='.',label=label);ax.set_title(('Left' if side==0 else 'Right')+' posterior source-band section');ax.set_xlabel('Component Y (cm)');ax.set_ylabel('Component X (cm)');ax.grid(alpha=.2);ax.legend()
fig.suptitle('Same source-ID height band, mapped through actual UE stage 03\nLocal sampled section; not a whole-body RMS or UE screenshot');fig.savefig(a.out/'local-sections.png',dpi=150);plt.close(fig)
