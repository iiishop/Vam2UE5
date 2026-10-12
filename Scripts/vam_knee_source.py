"""Retain the source knee operator, not a character-specific sculpt or sampled target."""
import copy
from vam_native_source import digest

def recover_knees(ir, contract, family):
    skin=next(r['parameters'] for r in ir['records'] if r.get('kind')=='skin' and str(r['object'])==str(contract['skin']['source_object']))
    if skin['_useGeneralWeights']:
        return {'version':1,'mode':'source_general_no_triax_correction','nodes':[]}
    leg=family['leg_jiggle']; names=[leg['left_shin'],leg['right_shin']]
    nodes=[copy.deepcopy(n) for n in skin['nodes'] if n['name'] in names]
    if set(n['name'] for n in nodes)!=set(names):raise ValueError('Missing source knee TriAx nodes')
    out={'version':1,'mode':'source_triax_knee_x','nodes':nodes,'bulge_scale':skin['bulgeScale'],
         'names':names,'source_axis_ue':[0,1,0],
         'scope':'Isolated source X (UE Y) weighted-angle and multiplicative bulge response; GPU morph reconstruction on final actual weights. No plugin targets or physics substitution.'}
    out['digest']=digest(out)
    return out
