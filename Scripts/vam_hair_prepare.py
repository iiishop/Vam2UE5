"""Prepare source-derived Groom curves. No hairstyle-name heuristics or body nearest-neighbour fitting.

Uses the retained DAZ scalp wrap and source->render map; preserves painted rigidity,
source strand IDs and source item boundaries. High-rigidity prefixes are explicit
head-bound anchor sections. Fully coiled/stiff curves remain elastic rods, not a rigid wig.
"""
import argparse,bisect,copy,hashlib,json,math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from vam_decode import validate_mesh
from vam_fit import fit_wrap
from vam_hair_catalog import catalog

def arclength(points):
    out=[0.0]
    for a,b in zip(points,points[1:]):out.append(out[-1]+math.dist(a,b))
    return out

def interp(x,xs,ys):
    if x<=xs[0]:return ys[0]
    if x>=xs[-1]:return ys[-1]
    i=bisect.bisect_right(xs,x)-1
    a=(x-xs[i])/(xs[i+1]-xs[i]);return ys[i]*(1-a)+ys[i+1]*a

def resample(points,n=32):
    arc=arclength(points)
    if arc[-1]<.00001:raise ValueError('Degenerate hair curve')
    return [[interp(arc[-1]*i/(n-1),arc,[p[k] for p in points]) for k in range(3)] for i in range(n)]

def prepare(ir,contract,extra=None,scalp_frames=True):
    body=copy.deepcopy(next(r['mesh'] for r in ir['records'] if r.get('class')=='DAZMergedMesh'))
    cb=contract['body'];x=copy.deepcopy(cb['vertices'])
    if contract['shape_convention']=='neutral_plus_parameters':
        for m in contract['morphs']:
            value=float(m['default'])
            if value:
                for point,delta in zip(x,m['deltas']):
                    for k in range(3):point[k]+=value*delta[k]
    # Explicit source map, never nearest point matching.
    for i,s in enumerate(cb['converted_to_source_vertex']):body['vertices'][s]=[x[i][k]/100 for k in (1,2,0)]
    scalps={r['name']:r for r in ir['records'] if r.get('kind')=='builtin_scalp'}
    records=extra or [r for r in ir['records'] if (r.get('data',{}).get('dynamic') or {}).get('hair')]
    groups=[];receipt=[]
    for record in records:
        d=record['data'];h=d['dynamic']['hair'];seg=h['segments'];src=[h['vertices'][i:i+seg] for i in range(0,len(h['vertices']),seg)]
        if h['scalp']=='CustomScalp':
            if len(d['meshes'])!=1 or not d['wraps']:raise ValueError('Ambiguous custom scalp')
            scalp=d['meshes'][0];wrap=d['wraps'][0]
        else:
            if h['scalp'] not in scalps:raise ValueError('Missing verified scalp '+h['scalp'])
            scalp=scalps[h['scalp']]['mesh'];wrap=scalps[h['scalp']]['wrap']
        wrap=copy.deepcopy(wrap)
        scalp_settings=wrap.get('scalp_settings',{})
        if h['scalp']!='CustomScalp' and not scalp_settings:
            raise ValueError('Source scalp metadata predates restoration; re-decode builtin scalp settings')
        if scalp_settings.get('moveToSurface'):
            for row in wrap['vertices']:row[4]=max(row[4],float(scalp_settings['moveToSurfaceOffset']))
        if int(scalp_settings.get('smoothOuterLoops',0)):
            raise ValueError('Source scalp smoothing requires validated transport before import')
        fitted=fit_wrap(scalp,wrap,body,surface_offset=float(scalp_settings.get('surfaceOffset',0)),thickness=float(scalp_settings.get('additionalThicknessMultiplier',0)));sm=validate_mesh(scalp)
        # fit_wrap returns the source UV domain, whereas source scalp vertices may be welded.
        source=scalp['vertices'];target=fitted['vertices']
        if len(target)==len(scalp['uv']):
            source=[source[i] for i in sm]
        if len(h['strands'])!=len(source):raise ValueError('Scalp/root domain mismatch')
        if scalp_frames:
            from vam_hair_frames import frames,transport
            source_frames=frames(scalp,source);target_frames=frames(scalp,target)
        settings=next((s for s in d['vaj']['storables'] if s['id'].endswith('Sim')),{}).copy()
        uid=d['vam'].get('uid','')+'Sim'
        for doc in ir.get('plan_documents',{}).values():
            for s in doc.get('parameters',{}).get('storables',[]):
                if s.get('id')==uid:settings.update(s)
        rigidity=[h['rigidities'][i:i+seg] for i in range(0,len(h['rigidities']),seg)] if h['rigidities'] else [[0.0]*seg for _ in src]
        if str(settings.get('usePaintedRigidity','true')).lower()!='true':
            u=[i/(seg-1) for i in range(seg)]
            values=[float(settings.get('tipRigidity',0))+(float(settings.get('mainRigidity',.01))-float(settings.get('tipRigidity',0)))*(1-t)**float(settings.get('rigidityRolloffPower',8)) for t in u]
            values[0]=1.1;values[1]=float(settings.get('rootRigidity',.2));rigidity=[values[:] for _ in src]
        buckets={};prefixes=[];lengths=[]
        for i,p in enumerate(src):
            s=h['root_to_scalp'][i]
            # Transport the entire authored strand through its source scalp frame.
            p=[[100*point[k] for k in (2,0,1)] for point in [transport(point,source[s],target[s],source_frames[s],target_frames[s]) for point in p]] if scalp_frames else [[100*(point[k]+target[s][k]-source[s][k]) for k in (2,0,1)] for point in p]
            rig=rigidity[i];prefix=1
            while prefix<seg-2 and rig[prefix]>=.98:prefix+=1
            full_stiff=bool(sum(rig[1:])/len(rig[1:])>.95)
            if full_stiff:prefix=1
            cut=max(0,prefix-1);prefixes.append(cut)
            if cut>0:
                buckets.setdefault('anchor',[]).append({'source_curve':i,'points':resample(p[:cut+1],max(2,min(16,cut+1))),'rigidity':1.0})
            tail=p[cut:];length=arclength(tail)[-1];lengths.append(length)
            stiff=sum(rig[cut+1:])/len(rig[cut+1:]);kind='coiled' if full_stiff else ('firm' if stiff>.3 else 'free')
            arc=arclength(tail)
            profile=[interp(arc[-1]*j/31,arc,rig[cut:]) for j in range(32)]
            buckets.setdefault(kind,[]).append({'source_curve':i,'points':resample(tail),'rigidity':stiff,'rigidity_profile':profile,'length_cm':length,'anchor_index':cut})
        for kind,curves in buckets.items():
            members={c['source_curve'] for c in curves}
            triangles=[h['indices'][i:i+3] for i in range(0,len(h['indices']),3)]
            triangles=[t for t in triangles if len(t)==3 and len(set(t))==3 and all(v in members for v in t)]
            groups.append({'name':Path(record['path']).stem+'_'+kind,'kind':kind,'curves':curves,'source_triangles':triangles,'settings':settings,'source_path':record['path']})
        receipt.append({'source':record.get('source',''),'path':record['path'],'scalp':h['scalp'],'curves':len(src),'source_points':seg,'prefix_points_min_max':[min(prefixes),max(prefixes)],'free_length_cm_min_max':[min(lengths),max(lengths)],'rigidity_range':[min(min(r) for r in rigidity),max(max(r) for r in rigidity)],'untranslated_render_controls':{k:v for k,v in settings.items() if k.startswith('curl') or k.startswith('length')},'nearby_groups_retained_count':sum(len(g) for g in h['nearby_groups'])})
    result={'schema':'vam-groom-source/1','groups':groups,'receipt':receipt,'body_contract':contract['contract_id'],'anchor_policy':'Contiguous painted rigidity >=0.98 prefix only; source curve identity retained; fully stiff/coiled curves retain elastic rod simulation. Interior tie networks require authored constraints; no inference by hairstyle name.','shape_policy':'Source scalp-frame transport with source surface projection constraints; authored guide identity retained. Render curl/length/strand-spread fidelity is tracked separately.'}
    catalog_ir=dict(ir,records=[r for r in ir['records'] if not (r.get('data',{}).get('dynamic') or {}).get('hair')]+records)
    result['catalog']=catalog(catalog_ir)
    result['identity']=hashlib.sha256(json.dumps(result,sort_keys=True,separators=(',',':')).encode()).hexdigest()
    return result

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('ir',type=Path);p.add_argument('contract',type=Path);p.add_argument('out',type=Path);p.add_argument('--extra',type=Path,nargs='*');a=p.parse_args()
    read=lambda p:json.loads(p.read_text(encoding='utf-8-sig'))
    extra=[{'path':read(p)['source_path'],'data':read(p)} for p in a.extra] if a.extra else None
    d=prepare(read(a.ir),read(a.contract),extra);a.out.parent.mkdir(parents=True,exist_ok=True);a.out.write_text(json.dumps(d,separators=(',',':')),encoding='utf-8');print(json.dumps({'identity':d['identity'],'groups':[(g['name'],len(g['curves'])) for g in d['groups']],'receipt':d['receipt']},indent=2))
