"""Compare existing C1.5 A1 replays with exact retained source IDs and VaM captures."""
import argparse,json,csv
from pathlib import Path
import numpy as np

def read(p):return json.loads(p.read_text(encoding="utf-8-sig"))
def surface(a,stage):
 m=read(a/("source-mapping.json" if stage=="00-source-lbs" else "mapping.json"));v=np.array(read(a/stage/"surface.json")["positions"])
 return {m["input_to_source"][j]:v[i] for i,j in enumerate(m["render_to_input"]) if 0<=j<len(m["input_to_source"]) and m["input_to_source"][j]>=0}
def main():
 ap=argparse.ArgumentParser();ap.add_argument("baseline",type=Path);ap.add_argument("candidate",type=Path);a=ap.parse_args();masks=np.load(a.baseline/"c2-region-masks.npz");ids=masks["source_ids"];regions={k:{int(ids[i]):i for i in np.flatnonzero(masks[k]>.05)} for k in masks.files if k!="source_ids"};rows=[];gates=[]
 old={(r["pose"],r["mode"]):r for r in read(a.baseline/"ue-replays.json")};new={(r["pose"],r["mode"]):r for r in read(a.candidate/"ue-replays.json")}
 for key in sorted(old.keys()&new.keys()):
  if key[1]!="on":continue
  capture=Path(old[key]["source_capture"]);meta=read(capture);target=np.fromfile(capture.parent/meta["raw"]["file"],dtype="<f4").reshape(-1,3)[:,[2,0,1]].astype(float)*100
  # Both replays consume the same converted actual matrices, same morph request and source capture.
  assert old[key]["request_id"]==new[key]["request_id"]
  bone_sets=[]
  for label,record in [("before",old[key]),("after",new[key])]:
   bundle=Path(record["audit_bundle"]);inp=read(bundle/"00-source-lbs/inputs.json");bone_sets.append({b["name"]:np.array(b["component_matrix_row_major"]) for b in inp["bones"]})
   for stage in ["00-source-lbs","01-final-weights","02-static-helpers","03-corrective"]:
    points=surface(bundle,stage)
    for region,mapping in regions.items():
     pairs=[(s,i) for s,i in mapping.items() if s in points];u=np.array([points[s] for s,i in pairs]);v=np.array([target[i] for s,i in pairs]);err=np.linalg.norm(u-v,axis=1)
     rows.append(dict(version=label,pose=key[0],stage=stage,region=region,count=len(err),rms_cm=float(np.sqrt(np.mean(err*err))),p95_cm=float(np.percentile(err,95)),max_cm=float(err.max()),worst_source=pairs[int(err.argmax())][0]))
  gates.append(dict(pose=key[0],same_request=True,source_matrix_max_difference=max(float(np.max(np.abs(bone_sets[0][b]-bone_sets[1][b]))) for b in bone_sets[0])))
 with (a.candidate/"regional-comparison.csv").open("w",newline="") as f:w=csv.DictWriter(f,fieldnames=list(rows[0]));w.writeheader();w.writerows(rows)
 (a.candidate/"comparison-gates.json").write_text(json.dumps({"gates":gates,"mapping":"retained source IDs only; source [z,x,y] meters to UE cm; same request hashes; masks threshold .05 from C1.5","scope":"VaM core-on raw surface, no plugin attribution and no claim all anatomical residuals are defects"},indent=2))
 print("regional rows",len(rows),"matrix max",max(g["source_matrix_max_difference"] for g in gates))
if __name__=="__main__":main()
