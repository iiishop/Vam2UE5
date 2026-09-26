"""Separate exterior skin and socket interiors using closed topology rims."""
import numpy as np
from scipy.sparse import coo_matrix
from scipy.sparse.csgraph import connected_components,dijkstra


def neighborhood(vertices,triangles,loops):
    """Physical support from eyelid perimeter, independent of mesh density."""
    edges=np.unique(np.sort(np.concatenate([triangles[:,[0,1]],triangles[:,[1,2]],triangles[:,[2,0]]]),axis=1),axis=0)
    lengths=np.linalg.norm(vertices[edges[:,0]]-vertices[edges[:,1]],axis=1)
    graph=coo_matrix((np.r_[lengths,lengths],(np.r_[edges[:,0],edges[:,1]],np.r_[edges[:,1],edges[:,0]])),shape=(len(vertices),len(vertices))).tocsr()
    seeds=np.unique(np.concatenate(loops))
    distance=dijkstra(graph,directed=False,indices=seeds,min_only=True)
    perimeter=np.mean([np.linalg.norm(vertices[np.roll(loop,-1)]-vertices[loop],axis=1).sum() for loop in loops])
    return set(np.flatnonzero(distance<=.2*perimeter))


def partition(triangles,count,loops):
    triangles=np.asarray(triangles,int);boundary=np.concatenate(loops)
    edges=np.concatenate([triangles[:,[0,1]],triangles[:,[1,2]],triangles[:,[2,0]]])
    cut=edges[~np.any(np.isin(edges,boundary),axis=1)]
    graph=coo_matrix((np.ones(len(cut)),(cut[:,0],cut[:,1])),shape=(count,count)).tocsr()
    _,labels=connected_components(graph,directed=False)
    active=np.unique(triangles);active=active[~np.isin(active,boundary)]
    owners,sizes=np.unique(labels[active],return_counts=True);outside=int(owners[sizes.argmax()])
    inside=[]
    for loop in loops:
        neighbors=np.unique(edges[np.any(np.isin(edges,loop),axis=1)])
        neighbors=neighbors[~np.isin(neighbors,boundary)]
        options=set(labels[neighbors])-{outside}
        if len(options)!=1:raise ValueError('AmbiguousSocketTopologyPartition')
        inside.append(int(next(iter(options))))
    if len(set(inside))!=len(loops):raise ValueError('SocketInteriorsNotSeparated')
    return labels,outside,inside


def ownership(source_triangles,target_triangles,source_count,target_count,source_loops,target_loops,target_vertices=None):
    sl,so,si=partition(source_triangles,source_count,source_loops)
    tl,to,ti=partition(target_triangles,target_count,target_loops)
    source_boundary=np.concatenate(source_loops);target_boundary=np.concatenate(target_loops)
    roi=set(target_boundary)
    for _ in range(3):
        roi.update(target_triangles[np.any(np.isin(target_triangles,list(roi)),axis=1)].ravel().tolist())
    if target_vertices is not None:roi=neighborhood(target_vertices,target_triangles,target_loops)
    groups=[]
    for name,source_label,target_label in [('exterior',so,to)]+[(f'socket_{i}',s,t) for i,(s,t) in enumerate(zip(si,ti))]:
        allowed=(sl==source_label);allowed[source_boundary]=True
        triangles=source_triangles[np.all(allowed[source_triangles],axis=1)]
        ids=np.array(sorted(i for i in roi if tl[i]==target_label and i not in target_boundary),int)
        if not len(triangles) or not len(ids):raise ValueError('EmptySocketCorrespondenceRegion')
        groups.append((name,ids,triangles))
    return groups
