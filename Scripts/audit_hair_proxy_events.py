"""Localize captured guide/proxy intersections without hiding attachment regions.

All points, the historical nonroot mask, automatic attachment prefix, and fully
free tail are reported together. Imported guide IDs are proved against captured
rest geometry in source order; no nearest-neighbour mapping is used.
"""
from __future__ import annotations

import argparse
import csv
import hashlib
import json
from pathlib import Path

import numpy as np


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def deepest(points, proxies):
    depth=np.full(len(points),-np.inf);owner=np.full(len(points),-1,dtype=int)
    for index,proxy in enumerate(proxies):
        a,b=np.asarray(proxy["a"],float),np.asarray(proxy["b"],float);edge=b-a
        t=np.clip((points-a)@edge/max(float(edge@edge),1e-20),0,1)
        current=float(proxy["radius_cm"])-np.linalg.norm(points-a-t[:,None]*edge,axis=1)
        selected=current>depth;depth[selected]=current[selected];owner[selected]=index
    return np.maximum(depth,0),owner


def identity_mapping(source,capture,replay,tolerance):
    paths=[Path(p) for p in replay["source_files"] if p.replace("\\","/").endswith("/01-final-weights/inputs.json")]
    if len(paths)!=1:raise ValueError("Need unique A1 inverse-bind inputs from replay provenance")
    path=paths[0]
    expected_hash=next(h for p,h in replay["source_files"].items() if Path(p)==path)
    if sha(path)!=expected_hash:raise ValueError("A1 bind source hash changed")
    inputs=read(path);head=next(b for b in inputs["bones"] if b["name"]=="head")
    inverse=np.array(head["inverse_bind_matrix_row_major"]).reshape(4,4).T
    proof=[]
    for group in capture["gpu_samples"][0]["groups"]:
        gid=group["group"];sg=source["groups"][gid]
        curves={c["source_curve"]:c for c in sg["curves"]}
        ids=sg["source_constraints"]["guide_source_ids"]
        p=np.asarray([curves[i]["points"] for i in ids],float)
        if p.shape[1:]!=(32,3) or len(p)!=group["guide_curves"]:raise ValueError("Unexpected imported guide layout")
        expected=(np.concatenate([p,np.ones((*p.shape[:2],1))],axis=-1)@inverse.T)[...,:3]
        actual=np.asarray(group["guide_rest_points"],float).reshape(-1,32,3)
        error=np.linalg.norm(actual-expected,axis=2)
        if error.max()>tolerance:raise ValueError(f"Ordered source-to-guide identity failed for {gid}: {error.max()} cm")
        proof.append(dict(group=gid,guide_curves=len(p),rms_cm=float(np.sqrt(np.mean(error**2))),max_cm=float(error.max()),
                          method="source_constraints.guide_source_ids -> source_curve -> A1 inverse head bind -> captured ordered rest points"))
    return proof,dict(path=str(path),sha256=sha(path),tolerance_cm=tolerance)


def audit(capture,source,replay,threshold=.2,mapping_tolerance=.02):
    mapping,bind=identity_mapping(source,capture,replay,mapping_tolerance)
    summary={};events=[]
    for frame_index,frame in enumerate(capture["gpu_samples"]):
        replay_index=int(round(frame["time"]*30))
        if not 0<=replay_index<len(replay["samples"]):raise ValueError("Replay index out of range")
        phase=replay["samples"][replay_index]["phase"]
        for group in frame["groups"]:
            gid=group["group"];sg=source["groups"][gid];constraint=sg["source_constraints"]
            guide_ids=constraint["guide_source_ids"];curves={c["source_curve"]:c for c in sg["curves"]}
            points,rest=np.asarray(group["guide_points"],float),np.asarray(group["guide_rest_points"],float)
            if points.shape!=rest.shape or not np.isfinite(points).all():raise ValueError("Invalid guide samples")
            local_index=np.tile(np.arange(32),len(guide_ids));u=local_index/31
            physical_coord=u*30;lower=np.floor(physical_coord+1e-9).astype(int);upper=np.minimum(lower+1,30)
            alpha=physical_coord-lower
            anchor=constraint.get("automatic_bundle_anchor")
            anchor_u=anchor["curve_coordinate"] if anchor else None
            nonroot=local_index>=3
            masks={"all_points":np.ones(len(points),bool),"historical_nonroot_j_ge_3":nonroot,
                   "root_prefix_j_lt_3":~nonroot}
            if anchor is not None:
                masks.update(nonroot_attachment_prefix=nonroot&(u<=anchor_u),
                    nonroot_after_anchor=nonroot&(u>anchor_u),
                    nonroot_straddling_solver_interval=nonroot&(lower/30<=anchor_u)&(upper/30>anchor_u),
                    nonroot_both_solver_nodes_after_anchor=nonroot&(lower/30>anchor_u))
            else:
                masks["nonroot_without_automatic_anchor"]=nonroot
            for region in ("head","body"):
                proxies=[p for p in frame["collision_proxies_head_local"] if (p["bone"]=="head")==(region=="head")]
                if not proxies:continue
                depth,owner=deepest(points,proxies);rest_depth,_=deepest(rest,proxies)
                for scope,mask in masks.items():
                    indices=np.flatnonzero(mask)
                    if not len(indices):continue
                    key=(phase,gid,region,scope)
                    row=summary.setdefault(key,dict(phase=phase,group=gid,name=sg["name"],region=region,scope=scope,
                        anchor_u=anchor_u,point_samples=0,over_threshold_samples=0,
                        over_threshold_and_authored_rest_inside_samples=0,
                        over_threshold_but_authored_rest_outside_samples=0,max_depth_cm=0,max_event=None))
                    above=indices[depth[indices]>threshold]
                    row["point_samples"]+=len(indices);row["over_threshold_samples"]+=len(above)
                    row["over_threshold_and_authored_rest_inside_samples"]+=int((rest_depth[above]>0).sum())
                    row["over_threshold_but_authored_rest_outside_samples"]+=int((rest_depth[above]<=0).sum())
                    index=int(indices[np.argmax(depth[indices])])
                    if depth[index]<=0:continue
                    gi,j=divmod(index,32);source_id=guide_ids[gi];curve=curves[source_id]
                    binding=curve.get("visible_binding",{})
                    rig=constraint["rigidity"][gi];raw=constraint["authored_rigidity"][gi]
                    # Physical node k maps to constraint slot k+1 (slot 0 virtual).
                    na,nb=min(int(lower[index])+1,31),min(int(upper[index])+1,31)
                    event=dict(phase=phase,group=gid,name=sg["name"],region=region,scope=scope,
                        time=frame["time"],geometry_sample=frame_index,replay_index=replay_index,
                        guide_index=gi,flat_point_index=index,point_index=j,guide_u=float(u[index]),
                        anchor_u=anchor_u,solver_node_u_lower=float(lower[index]/30),solver_node_u_upper=float(upper[index]/30),
                        solver_interpolation_alpha=float(alpha[index]),imported_source_curve_id=source_id,
                        source_control_u_used_by_importer=float(u[index]*binding.get("length",1)),
                        source_binding=binding,depth_cm=float(depth[index]),rest_depth_same_proxy_union_cm=float(rest_depth[index]),
                        authored_rest_inside_proxy=bool(rest_depth[index]>0),
                        position_head_local_cm=points[index].tolist(),rest_head_local_cm=rest[index].tolist(),
                        proxy_index_in_region=int(owner[index]),proxy=proxies[owner[index]],
                        lower_upper_support=[rig[na],rig[nb]],lower_upper_authored_support=[raw[na],raw[nb]])
                    events.append(event)
                    if depth[index]>row["max_depth_cm"]:
                        row["max_depth_cm"]=float(depth[index]);row["max_event"]=event
    rows=list(summary.values())
    for row in rows:row["fraction_over_threshold"]=row["over_threshold_samples"]/max(1,row["point_samples"])
    return dict(schema="vam-hair-proxy-event-localization/1",threshold_cm=threshold,
        mapping_proof=mapping,bind_source=bind,summary=rows,worst_per_frame_events=events,
        limitations=["These are interpolated Groom guide points, not raw Niagara solver particles.",
            "All points, old root exclusion, attachment prefix and free tail are reported together. No excluded population is silently declared safe.",
            "Automatic anchor identifies a control/support region, not anatomical evidence that its geometry is intentionally buried.",
            "Rest-inside tests use authored guide geometry against the same frame's proxy union. They are not direct readback of Niagara RestDistance or its collision enable bit.",
            "Even a free tail outside proxies does not prove rendered strands outside actual animated skin.",
            "Source IDs are selected visible guide identities with original source-curve bindings; they are not one-to-one VaM simulation-particle IDs."])


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("capture",type=Path);p.add_argument("source",type=Path);p.add_argument("replay",type=Path);p.add_argument("output",type=Path)
    p.add_argument("--threshold",type=float,default=.2);p.add_argument("--mapping-tolerance",type=float,default=.02);a=p.parse_args()
    result=audit(read(a.capture),read(a.source),read(a.replay),a.threshold,a.mapping_tolerance)
    result["files"]={str(path.resolve()):sha(path) for path in (a.capture,a.source,a.replay)}
    a.output.mkdir(parents=True,exist_ok=False)
    (a.output/"events.json").write_text(json.dumps(result,ensure_ascii=False,indent=2,allow_nan=False),encoding="utf-8")
    flat=[{k:v for k,v in row.items() if k!="max_event"} for row in result["summary"]]
    with (a.output/"summary.csv").open("w",newline="",encoding="utf-8-sig") as f:
        w=csv.DictWriter(f,fieldnames=list(flat[0]));w.writeheader();w.writerows(flat)
    print(json.dumps(dict(output=str(a.output),summary_rows=len(flat),worst_per_frame_events=len(result["worst_per_frame_events"])),ensure_ascii=False))


if __name__=="__main__":main()
