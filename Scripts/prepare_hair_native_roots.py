"""Explicit, body-specific native scalp import; never silently bless offline-fit error.

Reconstruct each original provider strand through its measured scalp matrix and
validate against independent physics-off particle readback before using it.
"""
import argparse, hashlib, json
from pathlib import Path
import numpy as np
from vam_hair_prepare import resample
from audit_hair_rest import audit

def main(capture, prepared, contract, output):
    d=json.loads(prepared.read_text()); c=json.loads(contract.read_text())
    m=json.loads((capture/'hair-rest.json').read_text())
    b=json.loads((capture/'hair-rest-body.json').read_text())
    assert d['body_contract']==c['contract_id'] and str(m['physics']).lower()=='false'
    before=audit(capture,d,c)
    head=np.array(next(x for x in b['bones'] if x['id']=='head')['worldMatrix'],float).reshape(4,4)
    bind=np.array(next(x for x in c['bones'] if x['name']=='head')['world_bind'],float)
    assets={x['source_path']:x for x in d['catalog']['assets']}; rows=[]
    for item in m['hairs']:
        groups=[g for g in d['groups'] if Path(g['source_path']).stem==item['path'].split(':')[-1]]
        assert len(groups)==1
        g=groups[0]; source=assets[g['source_path']]['source']['dynamic']['hair']
        provider=np.fromfile(capture/(item['prefix']+'-provider.f32'),'<f4').reshape(-1,3)
        assert np.array_equal(provider,np.array(source['vertices'],dtype='<f4'))
        ids=list(map(int,item['root_to_scalp'])); assert ids==source['root_to_scalp']
        p=provider.reshape(len(ids),int(item['source_segments']),3)
        tr=np.array(item['root_transforms'],float).reshape(-1,4,4)[ids]
        world=np.einsum('nij,nkj->nki',tr,np.concatenate([p,np.ones((*p.shape[:2],1))],-1))[...,:3]
        measured=np.fromfile(capture/(item['prefix']+'-particles.f32'),'<f4').reshape(p.shape)
        error=np.linalg.norm(world-measured,axis=-1)*100
        assert np.isfinite(world).all() and error.max()<.001, 'Native root reconstruction failed'
        local=(np.concatenate([world,np.ones((*world.shape[:2],1))],-1)@np.linalg.inv(head).T)[...,:3]
        local=local[:,:,[2,0,1]]*100
        fitted=(np.concatenate([local,np.ones((*local.shape[:2],1))],-1)@bind.T)[...,:3]
        for curve in g['curves']:
            assert curve['anchor_index']==0
            curve['points']=resample(fitted[curve['source_curve']].tolist(),len(curve['points']))
        rows.append(dict(resource=g['source_path'],particle_reconstruction_max_cm=float(error.max()),particle_reconstruction_rms_cm=float(np.sqrt(np.mean(error**2)))))
    d['native_root_fit']={'scope':'Body-specific measured scalp matrices; offline scalp fit discrepancies retained, not a general offline fitting fix','capture':str(capture.resolve()),'before':before,'reconstruction':rows,'capture_sha256':hashlib.sha256((capture/'hair-rest.json').read_bytes()).hexdigest()}
    d.pop('identity');d['identity']=hashlib.sha256(json.dumps(d,sort_keys=True,separators=(',',':'),allow_nan=False).encode()).hexdigest()
    output.write_text(json.dumps(d,separators=(',',':')))
    print(json.dumps(rows,indent=2))

if __name__=='__main__':
    p=argparse.ArgumentParser()
    for key in ['capture','prepared','contract','output']:p.add_argument(key,type=Path)
    a=p.parse_args();main(a.capture,a.prepared,a.contract,a.output)
