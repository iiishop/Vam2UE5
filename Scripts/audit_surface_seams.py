"""Source-ID seam audit of existing A1 exports; no proximity welding."""
import argparse,json,itertools
from pathlib import Path
import numpy as np

def read(p): return json.loads(p.read_text(encoding="utf-8-sig"))
def stats(v):
 a=np.array(v,float);return {"rms":float(np.sqrt(np.mean(a*a))),"p95":float(np.percentile(a,95)),"max":float(a.max())} if len(a) else None

def run(bundle):
 m=read(bundle/"mapping.json");source=[m["input_to_source"][i] if 0<=i<len(m["input_to_source"]) else -1 for i in m["render_to_input"]]
 groups={}
 for v,s in enumerate(source):
  if s>=0:groups.setdefault(s,[]).append(v)
 result={"bundle":str(bundle),"mapping_path":m["path"],"topology":m["topology_digest"],"pair_proof":"same retained source vertex ID; no nearest-neighbour matching", "stages":{}}
 for folder in sorted(bundle.iterdir()):
  if not (folder/"seam-attributes.json").exists() or folder.name.startswith("00-"):continue
  attrs=read(folder/"seam-attributes.json");vert=attrs["vertices"];inputs=read(folder/"inputs.json");positions=np.array(read(folder/"surface.json")["positions"]);weights=[dict(x) for x in inputs["render_vertex_influences"]]
  morphs={name:{r["vertex"]:r["delta"] for r in rows} for name,rows in attrs["morphs"].items()}
  rows=[]
  for s,group in groups.items():
   for a,b in itertools.combinations(group,2):
    va,vb=vert[a],vert[b]
    if set([va["material"],vb["material"]])!={0,19}:continue
    na,nb=np.array(va["normal"]),np.array(vb["normal"]);na/=np.linalg.norm(na);nb/=np.linalg.norm(nb)
    md={k:float(np.linalg.norm(np.array(v.get(a,[0,0,0]))-v.get(b,[0,0,0]))) for k,v in morphs.items() if a in v or b in v}
    rows.append({"source":s,"render_pair":[a,b],"vertices":[va,vb],"weights":[inputs["render_vertex_influences"][a],inputs["render_vertex_influences"][b]],"position_gap_cm":float(np.linalg.norm(positions[a]-positions[b])),"bind_gap_cm":float(np.linalg.norm(np.array(va["bind_position"])-vb["bind_position"])),"normal_angle_deg":float(np.degrees(np.arccos(np.clip(na@nb,-1,1)))),"weight_max_difference":max([abs(weights[a].get(k,0)-weights[b].get(k,0)) for k in weights[a].keys()|weights[b].keys()],default=0),"morph_delta_differences_cm":md,"morph_max_cm":max(md.values(),default=0)})
  result["stages"][folder.name]={"pairs":rows,"statistics":{key:stats([r[key] for r in rows]) for key in ["position_gap_cm","bind_gap_cm","normal_angle_deg","weight_max_difference","morph_max_cm"]}}
 return result
if __name__=="__main__":
 p=argparse.ArgumentParser();p.add_argument("bundle",type=Path);p.add_argument("--output",required=True,type=Path);a=p.parse_args();r=run(a.bundle);a.output.write_text(json.dumps(r,indent=2));print(json.dumps({k:v["statistics"] for k,v in r["stages"].items()},indent=2))
