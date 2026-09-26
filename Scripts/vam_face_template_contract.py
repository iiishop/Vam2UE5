"""Verify official vertex identity topology across DNA triangulation changes."""
import hashlib
import json
import math
from vam_face_topology_equivalence import equivalent,quad_triangles


def triangle_digest(triangles):
    return hashlib.sha256(json.dumps(triangles,separators=(',',':')).encode()).hexdigest()


def check(reference, actual, expected_digest):
    if triangle_digest(reference['triangles'])!=expected_digest:
        raise ValueError('TemplateReferenceTopologyChanged')
    if set(actual)!=set(range(len(reference['vertices']))):
        raise ValueError('OfficialHeadVertexIdentityMismatch')
    result=equivalent(reference['triangles'],actual)
    if result['quad_flips']:
        quads=reference.get('official_quads')
        if not quads:raise ValueError('OfficialQuadEvidenceRequired')
        quad_triangles(quads,reference['triangles']);quad_triangles(quads,actual)
        result['official_quad_coverage_verified']=True
    result['actual_triangle_sha256']=triangle_digest(actual)
    return result


def compare_reload(prior,actual,reference=None):
    if len(prior['vertices'])!=len(actual['vertices']):raise ValueError('ReloadVertexCountChanged')
    if not actual['vertices'] or any(len(v)!=3 or not all(math.isfinite(x) for x in v)
                                    for mesh in (prior,actual) for v in mesh['vertices']):
        raise ValueError('ReloadInvalidCoordinates')
    topology=None
    if prior['triangles']!=actual['triangles']:
        if reference is None:raise ValueError('ReloadTopologyChanged')
        count=len(reference['vertices'])
        def split(flat):
            skin=[];other=[]
            for i in range(0,len(flat),3):
                t=flat[i:i+3]
                (skin if max(t)<count else other).extend(t)
            return skin,other
        a,aux_a=split(prior['triangles']);b,aux_b=split(actual['triangles'])
        try:
            auxiliary=equivalent(aux_a,aux_b)
        except ValueError as exc:
            raise ValueError('ReloadAuxiliaryTopologyChanged') from exc
        digest=triangle_digest(reference['triangles'])
        topology={'before':check(reference,a,digest),'after':check(reference,b,digest),
                  'auxiliary':auxiliary}
    difference=max(abs(x-y) for a,b in zip(prior['vertices'],actual['vertices']) for x,y in zip(a,b))
    if difference>1e-5:raise ValueError('ReloadGeometryChanged:'+str(difference))
    return difference,topology
