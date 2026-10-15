"""Summarize real wall-frame samples separately from instrumented recordings."""
import argparse,csv,json
from pathlib import Path
import numpy as np
p=argparse.ArgumentParser();p.add_argument('run',type=Path);a=p.parse_args()
rows=list(csv.DictReader((a.run/'frames.csv').open(encoding='utf-8-sig')));out={}
for phase in dict.fromkeys(r['phase'] for r in rows):
 v=np.asarray([float(r['frame_ms']) for r in rows if r['phase']==phase]);assert len(v)==300 and np.isfinite(v).all()
 out[phase]=dict(frames=len(v),mean_ms=float(v.mean()),median_ms=float(np.median(v)),p95_ms=float(np.percentile(v,95)),mean_fps=float(1000/v.mean()))
base=(out['off_before']['mean_ms']+out['off_after']['mean_ms'])/2
result=dict(protocol=(a.run/'protocol.txt').read_text(encoding='utf-8-sig'),scope='Whole-frame wall time; hair rendering plus simulation delta, not isolated solver/GPU timing; no audit GPU readback.',phases=out,off_drift_ms=out['off_after']['mean_ms']-out['off_before']['mean_ms'],hair_delta_ms={k:out[k]['mean_ms']-base for k in ['fine','coarse']})
(a.run/'summary.json').write_text(json.dumps(result,indent=2));print(json.dumps(result,indent=2))
