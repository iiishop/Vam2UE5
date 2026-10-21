"""Combine immutable controls with an explicitly versioned mode-4-only rerun."""
import argparse,hashlib,json,os,shutil
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('controls',type=Path);p.add_argument('candidate',type=Path);p.add_argument('out',type=Path);a=p.parse_args()
read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
assert not a.out.exists()
old,new=read(a.controls/'identity.json'),read(a.candidate/'identity.json')
assert old['asset_identity']==new['asset_identity'] and old['recipe']==new['recipe']
changed=[k for k in old['source_hashes'] if old['source_hashes'][k]!=new['source_hashes'][k]]
assert changed==['Source\\VamCharacterRuntime\\Private\\VamLegJiggleProfile.cpp'],changed
# Reviewed diff: only MusclePath (called by mode 4) and its automation test changed.
# This assertion is not a general proof of semantic equivalence for arbitrary diffs.
allowed=Path('Source/VamCharacterRuntime/Private/VamLegJiggleProfile.cpp')
assert hashlib.sha256(allowed.read_bytes()).hexdigest()==new['source_hashes'][str(allowed)]
a.out.mkdir(parents=True);checks=[]
for g in ['M','R','S','C']:
 src=a.candidate if g=='M' else a.controls
 check=next(r for r in read(src/'validation.json') if r['group']==g);assert check['valid'];checks.append(check)
 shutil.copytree(src/g,a.out/g,copy_function=os.link)
manifest=dict(asset_identity=new['asset_identity'],configuration=new['configuration'],candidate=new,controls=old,changed_sources=changed,scope='M round 2; R/S/C unchanged mode paths reused from round 1. Separate source identities retained. Existing mesh/bind/morph/weights unchanged; not a new baked asset certificate.')
manifest['validation_identity']=hashlib.sha256(json.dumps(manifest,sort_keys=True).encode()).hexdigest()
(a.out/'identity.json').write_text(json.dumps(manifest,indent=2));(a.out/'validation.json').write_text(json.dumps(checks,indent=2))
print(a.out)
