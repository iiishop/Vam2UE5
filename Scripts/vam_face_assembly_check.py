"""Pure geometry check used on the actual assembled Face asset, not a preview."""
import math
from vam_face_topology_equivalence import equivalent


def compare_head(reference,actual):
    if len(reference['vertices'])!=len(actual['vertices']):raise ValueError('AssemblyHeadVertexCountChanged')
    topology=equivalent(reference['triangles'],actual['triangles'])
    errors=[]
    for a,b in zip(reference['vertices'],actual['vertices']):
        if len(a)!=3 or len(b)!=3 or not all(math.isfinite(x) for x in (*a,*b)):
            raise ValueError('AssemblyHeadInvalidCoordinates')
        errors.append(math.sqrt(sum((x-y)**2 for x,y in zip(a,b))))
    maximum=max(errors,default=float('inf'))
    if maximum>1e-4:raise ValueError('AssemblyChangedFittedHead:'+str(maximum))
    return {'head_vertex_count':len(errors),'max_rig_to_assembly_delta_cm':maximum,
            'topology_check':topology,'visual_acceptance_passed':False}
