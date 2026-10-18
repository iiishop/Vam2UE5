"""Bake a measured native static render evaluation into a body-fit Groom cache.

This is an explicit native-export import path, not an approximation claimed to be
the source shader. Reusable hair assets remain untouched. Provider IDs/geometry,
head matrices and body/style identities are checked before consuming GPU data.
"""
import argparse
import hashlib
import json
from pathlib import Path
import numpy as np
from audit_hair_rest import audit


def source_half_width_to_groom_diameter_cm(width_m):
    """Source ribbon vertices are centre +/- width; UE HairWidth is diameter."""
    width=float(width_m)
    if not np.isfinite(width) or width<=0:
        raise ValueError('Source strand half-width must be finite and positive')
    return width*200.0


def main(capture, prepared, contract, output):
    data=json.loads(prepared.read_text()); source=json.loads((capture/'hair-rest.json').read_text())
    body=json.loads((capture/'hair-rest-body.json').read_text()); contract_data=json.loads(contract.read_text())
    result=audit(capture,data,contract_data)
    # Native Unity buffers and matrices are float32. Bound roundoff in the
    # world->head->UE transform by coordinate scale, while retaining the 10um floor.
    coordinate_scale=max(abs(v) for g in data['groups'] for c in g['curves'] for p in c['points'] for v in p)
    tolerance=max(.001,64*np.finfo(np.float32).eps*coordinate_scale)
    result['transport_tolerance_cm']=float(tolerance)
    result['tolerance_policy']='max(0.001 cm, 64 * float32 epsilon * max source-fit coordinate in cm)'
    assert max(r['max_cm'] for r in result['resources'])<tolerance, 'Guide transport has not passed source comparison'
    head=np.array(next(b for b in body['bones'] if b['id']=='head')['worldMatrix'],float).reshape(4,4)
    bind=np.array(next(b for b in contract_data['bones'] if b['name']=='head')['world_bind'])
    assets={a['source_path']:a for a in data['catalog']['assets']}
    summaries=[]
    for item in source['hairs']:
        name=item['path'].split(':')[-1]; groups=[g for g in data['groups'] if Path(g['source_path']).stem==name]
        assert len(groups)==1,'Split physics groups require an explicit render binding; do not drop cross-group source triangles'
        group=groups[0]; hair=assets[group['source_path']]['source']['dynamic']['hair']
        provider=np.fromfile(capture/(item['prefix']+'-provider.f32'),dtype='<f4').reshape(-1,3)
        assert np.array_equal(provider,np.array(hair['vertices'],dtype='<f4')),'Capture is a different source resource'
        assert list(map(int,item['root_to_scalp']))==hair['root_to_scalp']
        count=len(hair['root_to_scalp']); density,detail=map(lambda x:int(float(x)),item['tess_factor'][:2])
        native=np.fromfile(capture/(item['prefix']+'-tess.f32'),dtype='<f4').reshape(-1,3)[:count*detail].reshape(count,detail,3)
        assert np.isfinite(native).all(), 'Native active tessellation range contains invalid points'
        local=(np.concatenate([native,np.ones((*native.shape[:2],1))],-1)@np.linalg.inv(head).T)[...,:3]
        local=local[:,:,[2,0,1]]*100
        points=(np.concatenate([local,np.ones((*local.shape[:2],1))],-1)@bind.T)[...,:3]
        bary=np.fromfile(capture/(item['prefix']+'-barycentrics.f32'),dtype='<f4').reshape(-1,3)
        assert len(bary)>=count*64 and np.allclose(bary.sum(-1),1,atol=1e-6)
        settings=item['render_settings']; segments=int(item['source_segments'])
        mapping={'length1':'Length1','length2':'Length2','length3':'Length3','maxSpread':'MaxSpread',
                 'curlScale':'WavinessScale','curlFrequency':'WavinessFrequency','curlRoot':'WavinessRoot',
                 'curlMid':'WavinessMid','curlTip':'WavinessTip','curlMidpoint':'WavinessMidpoint',
                 'spreadRoot':'InterpolationRoot','spreadMid':'InterpolationMid','spreadTip':'InterpolationTip'}
        for key,native_key in mapping.items():
            if key in group['settings']:
                assert abs(float(group['settings'][key])-float(settings[native_key]))<1e-5, 'Captured appearance changed: '+key
        assert settings['UseInterpolationCurves']=='false', 'Explicit source curve mode requires exporting the native interpolation attribute'
        midpoint=float(settings['InterpolationMidpoint']); split=max(.001,min(1,midpoint)); power=float(settings['InterpolationCurvePower'])
        root,mid,tip=[float(settings['Interpolation'+k]) for k in ('Root','Mid','Tip')]
        def interpolation(u):
            if u<=midpoint:return 1-(mid+(root-mid)*(1-u/split)**power)
            return 1-(mid+(tip-mid)*((u-split)/(1-split))**power)
        attrs=np.array([interpolation(i/(segments-1)) for i in range(segments)])
        interp=[]
        for j in range(detail):
            pos=segments*j/detail; a=min(int(pos),segments-1); b=min(a+1,segments-1)
            interp.append(attrs[a]+(attrs[b]-attrs[a])*(pos-int(pos)))
        lengths=np.array([float(settings['Length'+str(i)]) for i in (1,2,3)])
        spread=float(settings['MaxSpread'])*100
        triangles=np.array(hair['indices'],int).reshape(-1,3)
        curves=[]; bindings=[]
        for tri in triangles:
            assert tri.min()>=0 and tri.max()<count
            for line in range(density):
                y=line/density; k=min(63,int(y*64)); w=bary[int(tri[0])*64+k]
                length=float(bary[k]@lengths)
                ids=np.minimum(detail-1,np.floor((detail-.001)*np.arange(detail+1)/detail*length).astype(int))
                p0,p1,p2=points[tri][:,ids,:]
                def clamp_to_root(p):
                    delta=p-p0; size=np.linalg.norm(delta,axis=-1)
                    return p0+delta*(np.minimum(size,spread)/np.maximum(size,.01))[:,None]
                p1=clamp_to_root(p1);p2=clamp_to_root(p2)
                mixed=p0*w[0]+p1*w[1]+p2*w[2]
                mixed+=((p0+p1+p2)/3-mixed)*np.array(interp)[ids,None]
                curves.append(mixed.tolist())
                bindings.append(dict(source_curves=tri.tolist(),weights=w.tolist(),length=length))
        group['render_curves']=curves
        group['render_source_bindings']=bindings
        group['render_width_cm']=source_half_width_to_groom_diameter_cm(item['lod_settings']['FixedWidth'])
        summaries.append(dict(resource=name,render_curves=len(curves),native_samples_per_guide=detail,
                              source_triangles=len(triangles),physics=False,
                              source_half_width_m=float(item['lod_settings']['FixedWidth']),
                              groom_diameter_cm=group['render_width_cm']))
    data.pop('identity')
    data['native_render_evaluation']={'schema':'vam-native-hair-render-cache/1','capture':str(capture.resolve()),
        'manifest_sha256':hashlib.sha256((capture/'hair-rest.json').read_bytes()).hexdigest(),
        'input_hashes':{p.name:hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(capture.glob('*.f32'))},
        'style_identity':data['catalog']['hairstyle']['identity'],'body_contract':data['body_contract'],
        'scope':'Measured static native tessellation + source triangle/length/spread expansion; UE hair shader is a separate rendering model',
        'resources':summaries,'guide_validation':result}
    data['identity']=hashlib.sha256(json.dumps(data,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    output.write_text(json.dumps(data,separators=(',',':')));print(json.dumps(summaries,indent=2))


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('prepared',type=Path);p.add_argument('contract',type=Path);p.add_argument('output',type=Path);a=p.parse_args()
    main(a.capture,a.prepared,a.contract,a.output)
