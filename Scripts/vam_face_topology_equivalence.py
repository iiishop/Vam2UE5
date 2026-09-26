"""Accept only oriented triangle reorderings and provable quad diagonal flips.

An official DNA rebuild may triangulate the same quad with its other diagonal.
This does not permit vertex renumbering, arbitrary remeshing or winding changes.
"""
from collections import Counter, defaultdict
from pathlib import Path
import hashlib


def canonical(t):
    t=tuple(int(x) for x in t);i=t.index(min(t))
    return t[i:]+t[:i]


def triangles(flat):
    if len(flat)%3:raise ValueError('InvalidTriangleBuffer')
    result=Counter(canonical(flat[i:i+3]) for i in range(0,len(flat),3))
    if any(n!=1 or len(set(t))!=3 for t,n in result.items()):raise ValueError('DuplicateOrDegenerateTopology')
    return set(result)


def boundary(pair):
    directed=Counter((t[i],t[(i+1)%3]) for t in pair for i in range(3))
    return frozenset(e for e in directed if (e[1],e[0]) not in directed)


def quad_triangles(quads, flat):
    """Prove every mesh triangle belongs to exactly one official oriented quad."""
    actual=triangles(flat);covered=Counter();both=[]
    for q in quads:
        if len(q)!=4 or len(set(q))!=4:raise ValueError('InvalidOfficialQuad')
        a,b,c,d=map(int,q)
        choices=({canonical((a,b,c)),canonical((a,c,d))},
                 {canonical((a,b,d)),canonical((b,c,d))})
        matched=[pair for pair in choices if pair.issubset(actual)]
        if len(matched)!=1:raise ValueError('OfficialQuadTopologyMismatch')
        covered.update(matched[0]);both.extend([(a,b,c),(a,c,d),(a,b,d),(b,c,d)])
    if set(covered)!=actual or any(n!=1 for n in covered.values()):raise ValueError('OfficialQuadCoverageMismatch')
    return both


def with_official_quads(head, obj):
    obj=Path(obj);lines=obj.read_text(encoding='utf8').splitlines()
    if sum(line.startswith('v ') for line in lines)!=len(head['vertices']):raise ValueError('OfficialTemplateVertexCountMismatch')
    quads=[[int(x.split('/')[0])-1 for x in line.split()[1:]] for line in lines if line.startswith('f ')]
    quad_triangles(quads,head['triangles'])
    return dict(head,official_quads=quads,official_quad_provenance={
        'path':str(obj.resolve()),'sha256':hashlib.sha256(obj.read_bytes()).hexdigest(),
        'evidence':'Every original triangle covered exactly once by oriented official quad; no position matching'})


def equivalent(before,after):
    old=triangles(before);new=triangles(after)
    if len(old)!=len(new):raise ValueError('OfficialHeadConnectivityChanged')
    a=old-new;b=new-old
    if not a:return {'equivalent':True,'kind':'identical' if before==after else 'oriented_triangle_reorder','quad_flips':[]}
    adjacent=defaultdict(set)
    for t in a:
        for i in range(3):adjacent[tuple(sorted((t[i],t[(i+1)%3])))].add(t)
    proposals=[]
    for edge,pair in sorted(adjacent.items()):
        if len(pair)!=2:continue
        vertices=set().union(*map(set,pair))
        if len(vertices)!=4:continue
        others=sorted(vertices-set(edge));new_pair=set()
        # Try both windings; the oriented quad boundary must remain identical.
        for p in edge:
            for t in ((others[0],others[1],p),(others[1],others[0],p)):
                q=canonical(t)
                if q in b:new_pair.add(q)
        if len(new_pair)==2 and boundary(pair)==boundary(new_pair) and len(boundary(pair))==4:
            proposals.append((pair,new_pair,edge,tuple(others)))
    old_cover=Counter(t for pair,_,_,_ in proposals for t in pair)
    new_cover=Counter(t for _,pair,_,_ in proposals for t in pair)
    if set(old_cover)!=a or set(new_cover)!=b or any(n!=1 for n in (*old_cover.values(),*new_cover.values())):
        raise ValueError('OfficialHeadConnectivityChanged: not unique oriented quad flips')
    return {'equivalent':True,'kind':'oriented_quad_retriangulation',
            'quad_flips':[{'removed_diagonal':list(e),'added_diagonal':list(f)} for _,_,e,f in proposals]}
