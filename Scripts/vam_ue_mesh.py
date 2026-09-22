"""Adapt the WebGL inspection topology to UE clockwise geometry conventions."""
import math


def hair_reference_mesh(mesh, parameters):
    """Crossed tapered guide cards; a bounded reference, not VaM simulation."""
    vertices=[];uv=[];indices=[]
    width=max(.002,min(.15,float(parameters.get('width',.00025))*100))
    # Guide density is lower than rendered strand density. Approximate coverage
    # without inventing unbound roots or expanding the scalp attachment.
    width*=max(1,min(32,float(parameters.get('hairMultiplier',1))))
    source=mesh['vertices'];original=mesh['sections'][0]
    runs=[]
    for start in range(0,len(original),6):
        pair=min(original[start:start+6])//2
        if not runs or pair!=runs[-1][-1]+1:runs.append([pair])
        else:runs[-1].append(pair)
    for run in runs:
        points=[[sum(source[2*p+j][k]for j in (0,1))*.5 for k in range(3)]for p in run+[run[-1]+1]]
        for i in range(len(points)-1):
            a,b=points[i:i+2];t=[b[k]-a[k]for k in range(3)];length=math.sqrt(sum(x*x for x in t))
            if length<1e-9:continue
            t=[x/length for x in t];axis=[0.,0.,1.] if abs(t[2])<.9 else [1.,0.,0.]
            side=[t[1]*axis[2]-t[2]*axis[1],t[2]*axis[0]-t[0]*axis[2],t[0]*axis[1]-t[1]*axis[0]]
            length=math.sqrt(sum(x*x for x in side));side=[x/length for x in side]
            other=[t[1]*side[2]-t[2]*side[1],t[2]*side[0]-t[0]*side[2],t[0]*side[1]-t[1]*side[0]]
            for direction in (side,other):
                offset=len(vertices)
                for p,j in ((a,i),(b,i+1)):
                    v=j/(len(points)-1);radius=width*.5*max(.05,1-v*v)
                    for sign in (-1,1):
                        vertices.append([p[k]+sign*radius*direction[k]for k in range(3)]);uv.append([(sign+1)*.5,v])
                indices.extend([offset,offset+2,offset+1,offset+1,offset+2,offset+3])
    return {**mesh,'vertices':vertices,'uv':uv,'sections':[indices],
            'converted_to_source_vertex':list(range(len(vertices)))}


def normals_for_ue(mesh):
    # Accumulate across material boundaries and UV duplicates, but never weld
    # unrelated coincident vertices (mouth, eyelids, clothing, etc.).
    vertices = mesh['vertices']
    mapping = mesh.get('converted_to_source_vertex', list(range(len(vertices))))
    sums = {}
    for indices in mesh['sections']:
        for start in range(0, len(indices), 3):
            ids = indices[start:start+3]
            a, b, c = (vertices[i] for i in ids)
            u = [b[k]-a[k] for k in range(3)]
            v = [c[k]-a[k] for k in range(3)]
            # UE uses edge2.Cross(edge1); reversing the preview triangles below
            # therefore gives this outward normal.
            n = [u[1]*v[2]-u[2]*v[1], u[2]*v[0]-u[0]*v[2], u[0]*v[1]-u[1]*v[0]]
            for i in ids:
                total = sums.setdefault(mapping[i], [0., 0., 0.])
                for k in range(3): total[k] += n[k]
    result = []
    for key in mapping:
        n = sums.get(key, [0., 0., 1.])
        length = math.sqrt(sum(x*x for x in n))
        result.append([x/length for x in n] if length > 1e-20 else [0., 0., 1.])
    return result


def ue_triangles(indices, remap):
    return [(remap[indices[i]], remap[indices[i+2]], remap[indices[i+1]])
            for i in range(0, len(indices), 3)]
