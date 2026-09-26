"""Experimental neutral-p0 point/normal surface: Phong Tessellation (2008).

Preserves source vertices, but is not claimed to reproduce VaM subdivision.
"""
import numpy as np
from vam_face_fidelity import Surface
from vam_face_surface import closest,normals


def barycentric(q,t):
    ab=t[:,1]-t[:,0];ac=t[:,2]-t[:,0];aq=q-t[:,0]
    aa=np.sum(ab*ab,1);bb=np.sum(ac*ac,1);cc=np.sum(ab*ac,1);den=aa*bb-cc*cc
    u=np.divide(bb*np.sum(aq*ab,1)-cc*np.sum(aq*ac,1),den,out=np.zeros_like(den),where=abs(den)>1e-16)
    v=np.divide(aa*np.sum(aq*ac,1)-cc*np.sum(aq*ab,1),den,out=np.zeros_like(den),where=abs(den)>1e-16)
    return np.column_stack((1-u-v,u,v))


def evaluate(t,n,w,alpha=.5):
    linear=np.einsum('ni,nij->nj',w,t)
    projection=n*np.sum((linear[:,None,:]-t)*n,axis=2)[:,:,None]
    point=linear-alpha*np.einsum('ni,nij->nj',w,projection)
    derivatives=[]
    for axis in (1,2):
        edge=t[:,axis]-t[:,0];tangent=n*np.sum(edge[:,None,:]*n,axis=2)[:,:,None]
        derivatives.append(edge-alpha*(projection[:,axis]-projection[:,0]+np.einsum('ni,nij->nj',w,tangent)))
    normal=np.einsum('ni,nij->nj',w,n);normal/=np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-12)
    return point,normal,derivatives


class PhongSurface(Surface):
    def __init__(self,vertices,triangles):
        super().__init__(vertices,triangles)
        self.vertex_normals=normals(vertices,triangles)[0][triangles]

    def query(self,points,vertex_normals=None):
        k=min(32,len(self.triangles));_,indices=self.tree.query(points,k=k);indices=np.asarray(indices).reshape(len(points),k)
        hits=closest(np.repeat(points,k,axis=0),self.triangles[indices.ravel()]).reshape(len(points),k,3)
        distances=np.linalg.norm(hits-points[:,None,:],axis=2)
        if vertex_normals is not None:distances[np.sum(self.normals[indices]*vertex_normals[:,None,:],axis=2)<.65]=np.inf
        minimum=distances.min(1);tied=np.isfinite(distances)&(distances<=minimum[:,None]+1e-12)
        chosen=np.argmin(np.where(tied,indices,np.iinfo(np.int64).max),axis=1)
        row=np.arange(len(points));valid=np.isfinite(distances[row,chosen]);tri=indices[row,chosen]
        t=self.triangles[tri];n=self.vertex_normals[tri];w=barycentric(hits[row,chosen],t)
        for _ in range(3):
            p,_,(du,dv)=evaluate(t,n,w);error=points-p
            aa=np.sum(du*du,1);bb=np.sum(dv*dv,1);cc=np.sum(du*dv,1);det=aa*bb-cc*cc
            eu=np.sum(error*du,1);ev=np.sum(error*dv,1)
            u=np.divide(bb*eu-cc*ev,det,out=np.zeros_like(det),where=abs(det)>1e-16)
            v=np.divide(aa*ev-cc*eu,det,out=np.zeros_like(det),where=abs(det)>1e-16)
            w[:,1]+=u;w[:,2]+=v;w[:,0]=1-w[:,1]-w[:,2]
            w=np.maximum(w,0);w/=np.maximum(w.sum(1)[:,None],1e-12)
        p,normal,_=evaluate(t,n,w);distance=np.linalg.norm(p-points,axis=1);distance[~valid]=np.inf
        return p,normal,distance
