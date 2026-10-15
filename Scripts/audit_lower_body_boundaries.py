"""Source-provenance boundary and mirror diagnostics; plots are numeric, not UE renders."""
import argparse,json
from pathlib import Path
import numpy as np

def read(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def stats(x):
 x=np.asarray(x);return dict(count=len(x),rms=float(np.sqrt(np.mean(x*x))),p95=float(np.percentile(x,95)),max=float(x.max())) if len(x) else dict(count=0)
def bundle(root,pose):return Path(read(root/pose/'state.json')['audit_bundle'])
def data(b,stage):
 m=read(b/('source-mapping.json' if stage=='00-source-lbs' else 'mapping.json'));ids=np.array([m['input_to_source'][i] if 0<=i<len(m['input_to_source']) else -1 for i in m['render_to_input']]);v=np.array(read(b/stage/'surface.json')['positions']);return ids,v

def analyze(root,pose,pairs):
 b=bundle(root,pose);sid,source=data(b,'00-source-lbs');src={int(k):v for k,v in zip(sid,source) if k>=0};rows=[]
 for stage in ['01-final-weights','02-static-helpers','03-corrective','04-jiggle']:
  if not (b/stage/'surface.json').exists():continue
  ids,v=data(b,stage);points={int(k):p for k,p in zip(ids,v) if k>=0};valid=[p for p in pairs if all(k in points and k in src for k in p)];mirror=np.array([1,-1,1]);error=np.array([(points[l]-src[l])-(points[r]-src[r])*mirror for l,r in valid]);length=np.linalg.norm(error,axis=1);r=dict(pose=pose,stage=stage,extra_mirror_cm=stats(length),worst_pair=valid[int(length.argmax())]);rows.append(r)
 return rows

def boundary(root,pose):
 b=bundle(root,pose);stage='03-corrective';ids,v=data(b,stage);inp=read(b/stage/'inputs.json');attrs=read(b/stage/'seam-attributes.json');print('ATTR_KEYS',list(attrs)) if False else None
 # Triangle membership comes from exported section IDs, not spatial proximity.
 vertices=attrs['vertices'];mats=np.array([x['material'] for x in vertices]);tri=np.array(inp['triangle_indices']).reshape(-1,3);cross=np.cross(v[tri[:,1]]-v[tri[:,0]],v[tri[:,2]]-v[tri[:,0]]);length=np.linalg.norm(cross,axis=1);norm=cross/np.maximum(length[:,None],1e-30);edges={}
 for t,f in enumerate(tri):
  mat=int(mats[f[0]])
  if mat not in (0,19) or length[t]<1e-12:continue
  for a,c in zip(f,np.roll(f,-1)):
   key=tuple(sorted((int(ids[a]),int(ids[c]))))
   if key[0]<0 or key[0]==key[1]:continue
   edges.setdefault(key,[]).append((mat,t))
 rows=[]
 for edge,values in edges.items():
  a=[t for m,t in values if m==0];c=[t for m,t in values if m==19]
  for x in a:
   for y in c:rows.append(dict(source_edge=edge,dihedral_deg=float(np.degrees(np.arccos(np.clip(norm[x]@norm[y],-1,1))))))
 return dict(pose=pose,dihedral=stats([x['dihedral_deg'] for x in rows]),edges=rows)

def main():
 p=argparse.ArgumentParser();p.add_argument('root',type=Path);p.add_argument('--pairs',type=Path,required=True);p.add_argument('--output',type=Path,required=True);a=p.parse_args();pairs=read(a.pairs)['pairs'];rows=[];bounds=[]
 for state in a.root.glob('*/state.json'):
  pose=state.parent.name;rows.extend(analyze(a.root,pose,pairs));bounds.append(boundary(a.root,pose))
 a.output.write_text(json.dumps(dict(scope='Source-ID matched extra deformation asymmetry; not forced preset symmetry. Boundary edges from shared source IDs and actual section material IDs.',mirror=rows,boundary=bounds),indent=2));print('poses',len(bounds))
if __name__=='__main__':main()
