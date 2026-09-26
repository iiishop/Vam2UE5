"""Confidence-aware experimental residual fit; never invent reviewed semantics.

Source material identity defines the surface. Source open edge loops constrain
MH eyelid curves as explicitly unverified automatic candidates. Other official
curves are measured, not mislabeled as source correspondence. No identity IDs
or character-space coordinates participate in the algorithm.
"""
import numpy as np
from scipy import sparse
from scipy.sparse.linalg import cg, LinearOperator
from scipy.optimize import linear_sum_assignment
from scipy.spatial import cKDTree
from vam_face_fidelity import Surface, operators, triangle_check, contour, view_matrix, VIEWS
from vam_face_surface import normals
from vam_face_topology_calibrate import boundary_chains
from vam_face_topology_equivalence import quad_triangles
from vam_face_eye_correspondence import ordered_cycle, ordered_correspondence
from vam_face_eye_intersections import candidate_pairs, crossing_mask, crossings, pair_distances
from vam_face_eye_regions import ownership as eye_surface_ownership,neighborhood as eye_neighborhood

PROFILES = {'Conservative': (4, 8., 4.), 'Balanced': (8, 3., 8.),
            'BoundaryStrong': (8, 3., 24.), 'SurfaceStrong': (12, 1., 8.)}
FACE = ('Face', 'Head', 'Ears', 'Lips', 'Nostrils')


def arap_edges(base,current,edges):
    rest=base[edges[:,1]]-base[edges[:,0]];now=current[edges[:,1]]-current[edges[:,0]]
    cov=np.zeros((len(base),3,3));outer=np.einsum('ni,nj->nij',now,rest)
    np.add.at(cov,edges[:,0],outer);np.add.at(cov,edges[:,1],outer)
    u,_,vt=np.linalg.svd(cov);sign=np.linalg.det(u@vt);u[:,:,-1]*=sign[:,None]
    rotation=u@vt
    return np.einsum('nij,nj->ni',.5*(rotation[edges[:,0]]+rotation[edges[:,1]]),rest)


def local_guard(base,current,delta,triangles,edges,lengths,eye_triangles=None,skin_triangles=None,clearance_fraction=0.,prune_separated=True):
    """Limit only offending vertex steps, then independently validate globally."""
    old=np.cross(base[triangles[:,1]]-base[triangles[:,0]],base[triangles[:,2]]-base[triangles[:,0]])
    area2=np.sum(old*old,axis=1); valid=area2>1e-24; factors=np.ones(len(base))
    pairs=None
    if eye_triangles is not None:
        eye_lengths=np.linalg.norm(base[eye_triangles[:,1]]-base[eye_triangles[:,0]],axis=1)
        padding=clearance_fraction*float(np.median(eye_lengths))
        pairs=candidate_pairs(current,eye_triangles,skin_triangles,True,current+delta,padding)
        clearance=np.minimum(.1*pair_distances(base,eye_triangles,skin_triangles,pairs),padding) if padding else None
        initial_distance=pair_distances(current,eye_triangles,skin_triangles,pairs) if prune_separated else None
    for _ in range(40):
        trial=current+delta*factors[:,None]
        new=np.cross(trial[triangles[:,1]]-trial[triangles[:,0]],trial[triangles[:,2]]-trial[triangles[:,0]])
        # Keep an explicit interior margin: official Template fitting introduces
        # small numerical changes, so stopping on the validation boundary is
        # not a safe deliverable even when the offline iterate barely passes.
        bad_tri=valid&(np.sum(old*new,axis=1)<=.20*area2)
        ratio=np.divide(np.linalg.norm(trial[edges[:,1]]-trial[edges[:,0]],axis=1),lengths,out=np.ones_like(lengths),where=lengths>1e-10)
        bad_edge=(ratio<=.55)|(ratio>=1.45)
        collision_ids=np.array([],int)
        if pairs is not None:
            active=np.ones(len(pairs),bool)
            if initial_distance is not None:
                # Distance between sets can decrease by at most the sum of
                # their maximum vertex displacements (Hausdorff bound).
                # Skip only pairs provably unable to reach the barrier.
                movement=np.linalg.norm(trial-current,axis=1)
                bound=movement[eye_triangles[pairs[:,0]]].max(axis=1)+movement[skin_triangles[pairs[:,1]]].max(axis=1)
                active=initial_distance<=bound+(clearance if clearance is not None else 0.)+1e-12
            checked=pairs[active]
            bad=crossing_mask(trial,eye_triangles,skin_triangles,checked)
            if clearance is not None:bad|=pair_distances(trial,eye_triangles,skin_triangles,checked)<clearance[active]
            crossing=checked[bad]
            collision_ids=np.r_[eye_triangles[crossing[:,0]].ravel(),skin_triangles[crossing[:,1]].ravel()]
        if not bad_tri.any() and not bad_edge.any() and not len(collision_ids):return trial,factors
        ids=np.unique(np.concatenate([triangles[bad_tri].ravel(),edges[bad_edge].ravel(),collision_ids]))
        factors[ids]*=.5
        factors[factors<1e-10]=0
    # Saturation near the numeric barrier must retain the last safe iterate,
    # not discard already valid profiles or publish the offending proposal.
    ratio=np.divide(np.linalg.norm(current[edges[:,1]]-current[edges[:,0]],axis=1),lengths,out=np.ones_like(lengths),where=lengths>1e-10)
    if triangle_check(base,current,triangles)==0 and ratio.min()>.5 and ratio.max()<1.5:
        return current.copy(),np.zeros(len(base))
    raise ValueError('InvalidCurrentGeometry')


def segments(points, starts, ends):
    edge=ends-starts; denom=np.sum(edge*edge,axis=1)
    f=np.clip(np.einsum('nki,ki->nk',points[:,None,:]-starts,edge)/np.maximum(denom,1e-15),0,1)
    hits=starts[None,:,:]+f[:,:,None]*edge
    d=np.linalg.norm(hits-points[:,None,:],axis=2); i=d.argmin(1)
    return hits[np.arange(len(points)),i], i, f[np.arange(len(points)),i]


def tangent_edges(current, edges, target_normals):
    """Project current edges onto the measured source tangent field."""
    edge = current[edges[:,1]]-current[edges[:,0]]
    normal = target_normals[edges[:,0]]+target_normals[edges[:,1]]
    normal /= np.maximum(np.linalg.norm(normal,axis=1)[:,None],1e-12)
    return edge-normal*np.sum(edge*normal,axis=1)[:,None]


class AdaptiveFit:
    def __init__(self, source, head, landmarks):
        sv=np.asarray(source['vertices'],float); self.tt=np.asarray(head['triangles']).reshape(-1,3)
        self.source=source; self.head=head; self.landmarks=landmarks
        st=np.asarray(source['triangles']).reshape(-1,3)
        self.sf_ids=np.flatnonzero(np.isin(source['triangle_materials'],FACE)); self.sf=st[self.sf_ids]
        self.si=np.unique(self.sf); self.origin=sv[self.si].mean(0)
        self.scale=float(np.linalg.norm(np.ptp(sv[self.si],axis=0)))
        if source.get('normalization')=='rms':
            self.scale=float(2*np.sqrt(np.mean(np.sum((sv[self.si]-self.origin)**2,axis=1))))
        self.s=(sv-self.origin)/self.scale; self.base=(np.asarray(head['vertices'])-self.origin)/self.scale
        self.surface=Surface(self.s,self.sf)
        self.raw_surface=self.surface
        if source.get('surface_interpolation')=='phong':
            from vam_face_phong import PhongSurface
            self.surface=PhongSurface(self.s,self.sf)
        hits,ns,d=self.surface.query(self.base); vn,_=normals(self.base,self.tt)
        # Compare against the entire source skin to separate neck from face by
        # material ownership, not a fixed world-space crop or character ID.
        all_distance=Surface(self.s,st).query(self.base)[2]
        raw_distance=self.raw_surface.query(self.base)[2]
        selected=(raw_distance<=all_distance+1e-7)&(raw_distance<.06)&(np.sum(vn*ns,axis=1)>.25)
        self.tf_ids=np.flatnonzero(np.all(selected[self.tt],axis=1)); self.tf=self.tt[self.tf_ids]
        self.ids=np.unique(self.tf); self.fixed=np.setdiff1d(np.arange(len(self.base)),self.ids)
        self.edges,self.inc,self.lap=operators(self.tt,len(self.base))
        self.lengths=np.linalg.norm(self.inc@self.base,axis=1)
        self.guard_triangles=self.tt;self.guard_edges=self.edges;self.guard_lengths=self.lengths
        if head.get('official_quads'):
            self.guard_triangles=np.asarray(quad_triangles(head['official_quads'],head['triangles']),int)
            gt=self.guard_triangles
            self.guard_edges=np.unique(np.sort(np.concatenate([gt[:,[0,1]],gt[:,[1,2]],gt[:,[2,0]]]),axis=1),axis=0)
            self.guard_lengths=np.linalg.norm(self.base[self.guard_edges[:,1]]-self.base[self.guard_edges[:,0]],axis=1)
        self.correspondence=[]; self.boundary_ids=[]; self.boundary_points=[]; self.eye_boundary_ids=[]
        source_eye_loops=[];target_eye_loops=[]
        edge,count=np.unique(np.sort(np.concatenate([self.sf[:,[0,1]],self.sf[:,[1,2]],self.sf[:,[2,0]]]),axis=1),axis=0,return_counts=True)
        loops=[x['vertex_ids'] for x in boundary_chains(edge[count==1].tolist()) if x['closed'] and x['ordered']]
        # Render triangle diagonals are not original topology edges. Optional
        # neutral-source polygons allow annular candidate discovery without
        # confusing the socket opening with the visible eyelid rim.
        original_edges=set()
        for polygon in source.get('source_polygons',[]):
            original_edges.update(tuple(sorted((a,b))) for a,b in zip(polygon,polygon[1:]+polygon[:1]))
        original_edges=np.array(sorted(original_edges),int).reshape(-1,2)
        if len(original_edges):original_edges=original_edges[np.all(np.isin(original_edges,self.si),axis=1)]
        loop_families=[]
        for loop in loops:
            candidates=[(0,loop)];front=set(loop);seen=set(loop)
            for ring in range(1,5):
                if not len(original_edges):break
                next_ids=set(original_edges[np.any(np.isin(original_edges,list(front)),axis=1)].ravel())-seen
                front=next_ids;seen|=front
                if not front:break
                induced=original_edges[np.all(np.isin(original_edges,list(front)),axis=1)]
                for chain in boundary_chains(induced.tolist()):
                    if chain['closed'] and chain['ordered'] and .5*len(loop)<=len(chain['vertex_ids'])<=2*len(loop):
                        candidates.append((ring,chain['vertex_ids']))
            loop_families.append(candidates)
        eye_curves=[np.unique(landmarks['crv_eyelid_upper_'+side]['vIDs']+landmarks['crv_eyelid_lower_'+side]['vIDs']) for side in ('l','r')]
        choice={};costs=np.empty((len(eye_curves),len(loops)))
        for row,curve in enumerate(eye_curves):
            for col,candidates in enumerate(loop_families):
                scored=[]
                for ring,loop in candidates:
                    points=self.s[loop];hits,_,_=segments(self.base[curve],points,np.roll(points,-1,axis=0))
                    score=float(np.mean(np.linalg.norm(hits-self.base[curve],axis=1)))
                    score+=.25*float(cKDTree(self.base[curve]).query(points)[0].mean())
                    scored.append((score,ring,loop))
                picked=min(scored,key=lambda x:x[0]);costs[row,col]=picked[0];choice[row,col]=picked
        rows,cols=linear_sum_assignment(costs)
        for row,col in zip(rows,cols):
            # A large or ambiguous geometric assignment is not accepted.
            cost=costs[row,col]; alternatives=np.delete(costs[row],col)
            if cost>.04 or (len(alternatives) and alternatives.min()<cost*1.5): continue
            _,ring,selected_loop=choice[row,col]
            ids=eye_curves[row]; loop=np.array(selected_loop); points=self.s[loop]
            ordering=None
            if source.get('eyelid_correspondence')=='ordered-cycle':
                if not head.get('official_quads'):raise ValueError('OrderedEyelidRequiresOfficialQuads')
                ids=ordered_cycle(ids,head['official_quads'])
                curve_normals=normals(self.s,self.sf)[0][loop] if source.get('eye_curve_interpolation')=='phong' else None
                hits,index,fraction,ordering=ordered_correspondence(self.base[ids],points,curve_normals)
                self.eye_boundary_ids.extend(ids.tolist())
                source_eye_loops.append(loop);target_eye_loops.append(ids)
            else:
                hits,index,fraction=segments(self.base[ids],points,np.roll(points,-1,axis=0))
            self.boundary_ids.extend(ids.tolist()); self.boundary_points.extend(hits.tolist())
            self.correspondence.append({'semantic_id':'eyelid.'+('left' if row==0 else 'right'),
                'verification_state':'unverified','evidence':'source polygon-edge annulus candidate + official MH eyelid vertex IDs',
                'selected_topological_ring':ring,'candidate_count':len(loop_families[col]),
                'source_vertex_ids':[source['input_to_source_vertex'][i] for i in loop],
                'target_vertex_ids':ids.tolist(),'segment_indices':index.tolist(),'segment_fractions':fraction.tolist(),
                'assignment_distance_normalized':float(cost),'ordered_cycle_diagnostics':ordering})
        if source.get('boundary_constraints')=='material-and-annulus':
            ownership={};offset=0
            for polygon in source.get('source_polygons',[]):
                material=source['triangle_materials'][offset];offset+=len(polygon)-2
                for a,b in zip(polygon,polygon[1:]+polygon[:1]):ownership.setdefault(tuple(sorted((a,b))),set()).add(material)
            groups=[('lip_outer',('Face','Lips'),[np.unique(sum([landmarks['crv_lip_'+part+'_outer_'+side]['vIDs'] for part in ('upper','lower') for side in ('l','r')],[]))]),
                    ('nostril',('Face','Nostrils'),[np.array(landmarks['crv_nostril_'+side]['vIDs']) for side in ('l','r')])]
            for semantic,materials,curves in groups:
                borders=[list(e) for e,m in ownership.items() if set(materials).issubset(m)]
                candidates=[q['vertex_ids'] for q in boundary_chains(borders) if q['closed'] and q['ordered']]
                if len(candidates)<len(curves):continue
                costs=np.array([[np.linalg.norm(self.base[c].mean(0)-self.s[q].mean(0)) for q in candidates] for c in curves])
                rows,cols=linear_sum_assignment(costs)
                for row,col in zip(rows,cols):
                    if costs[row,col]>.04:continue
                    ids=curves[row];loop=np.array(candidates[col]);points=self.s[loop]
                    hits,index,fraction=segments(self.base[ids],points,np.roll(points,-1,axis=0))
                    self.boundary_ids.extend(ids.tolist());self.boundary_points.extend(hits.tolist())
                    self.correspondence.append({'semantic_id':semantic+'.'+str(row),'verification_state':'unverified',
                        'evidence':'source material boundary edges + official target curve; anatomical pairing remains provisional',
                        'source_vertex_ids':[source['input_to_source_vertex'][i] for i in loop],'target_vertex_ids':ids.tolist(),
                        'segment_indices':index.tolist(),'segment_fractions':fraction.tolist()})
        self.boundary_ids=np.asarray(self.boundary_ids,int); self.boundary_points=np.asarray(self.boundary_points).reshape(-1,3)
        self.fixed=np.setdiff1d(self.fixed,self.boundary_ids)
        self.surface_ids=np.setdiff1d(self.ids,self.eye_boundary_ids)
        self.eye_surfaces=[];self.eye_region_diagnostics=[]
        self.eye_sampling_factor=max(1.,min(16.,float(np.mean([len(t)/len(s) for s,t in zip(source_eye_loops,target_eye_loops)]))**2)) if source_eye_loops else 1.
        if source.get('eye_surface_ownership'):
            if len(source_eye_loops)!=2:raise ValueError('EyeOwnershipRequiresTwoOrderedRims')
            groups=eye_surface_ownership(self.sf,self.tt,len(self.s),len(self.base),source_eye_loops,target_eye_loops,
                                        self.base if source.get('eye_neighborhood')=='geodesic' else None)
            for name,ids,triangles in groups:
                region_surface=type(self.surface)(self.s,triangles)
                if hasattr(region_surface,'vertex_normals'):
                    # Restrict correspondence ownership without changing the
                    # reference surface at cut edges. Recomputing normals on
                    # a subset changes the Phong patch and fights the rim.
                    region_surface.vertex_normals=normals(self.s,self.sf)[0][triangles]
                self.eye_surfaces.append((ids,region_surface))
                self.eye_region_diagnostics.append({'region':name,'target_vertices':ids.tolist(),
                    'source_triangles':triangles.tolist(),'evidence':'connected components after cutting provisional topology rims',
                    'source_semantic_verified':False})
        self.eye_triangles=None
        if source.get('eye_self_intersection_guard'):
            roi=set(self.eye_boundary_ids)
            if not roi:raise ValueError('EyeGuardRequiresOrderedCorrespondence')
            for _ in range(3):roi.update(self.tt[np.any(np.isin(self.tt,list(roi)),axis=1)].ravel().tolist())
            if source.get('eye_neighborhood')=='geodesic':roi=eye_neighborhood(self.base,self.tt,target_eye_loops)
            self.eye_triangles=self.tt[np.any(np.isin(self.tt,list(roi)),axis=1)]
            if crossings(self.base,self.eye_triangles,self.tt,True):raise ValueError('InitialEyeSkinSelfIntersection')
        self.metric_map={'source_face_triangles':self.sf_ids.tolist(),'target_face_triangles':self.tf_ids.tolist(),'display_full_geometry':True}
        self.source_contours={v:contour(self.s,self.sf,view_matrix(*v))[1] for v in VIEWS}

    def query_fit_surface(self,v,ids,vn):
        hits,ns,d=self.surface.query(v[ids],vn[ids])
        for region_ids,surface in self.eye_surfaces:
            rows=np.flatnonzero(np.isin(ids,region_ids))
            if len(rows):
                # Once wall ownership is known, a folded initial normal must
                # not veto the correct wall and leave the initial crease fixed.
                guide=None if self.source.get('eye_tangent_refinement') else vn[ids[rows]]
                hits[rows],ns[rows],d[rows]=surface.query(v[ids[rows]],guide)
        return hits,ns,d

    def metrics(self,v):
        _,ns,d=self.surface.query(v[self.ids]); vn,_=normals(v,self.tt)
        reverse=Surface(v,self.tf).query(self.s[self.si])[2]
        regions={}
        for key,item in self.landmarks.items():
            ids=item.get('vIDs',[])
            if ids and any(x in key for x in ('eyelid','nostril','nasolabial','lip_','ear_','skull')):
                q=self.surface.query(v[ids])[2]*self.scale
                regions[key]={'surface_mean_cm':float(q.mean()),'surface_max_cm':float(q.max()),'source_semantic_verified':False}
        silhouettes={}
        for view in VIEWS:
            p=self.source_contours[view];q=contour(v,self.tf,view_matrix(*view))[1]
            silhouettes[str(view)]=float(.5*(cKDTree(p).query(q)[0].mean()+cKDTree(q).query(p)[0].mean()))
        surface=float(.5*(d.mean()+reverse.mean())); normal=float(np.mean(1-np.clip(np.sum(vn[self.ids]*ns,axis=1),-1,1)))
        boundary=float(np.linalg.norm(v[self.boundary_ids]-self.boundary_points,axis=1).mean()) if len(self.boundary_ids) else 0.
        guard_length=np.linalg.norm(v[self.guard_edges[:,1]]-v[self.guard_edges[:,0]],axis=1)
        ratio=np.divide(guard_length,self.guard_lengths,out=np.ones_like(self.guard_lengths),where=self.guard_lengths>1e-10)
        raw_distance=self.raw_surface.query(v[self.ids])[2]
        base_cross=np.cross(self.base[self.tt[:,1]]-self.base[self.tt[:,0]],self.base[self.tt[:,2]]-self.base[self.tt[:,0]])
        fit_cross=np.cross(v[self.tt[:,1]]-v[self.tt[:,0]],v[self.tt[:,2]]-v[self.tt[:,0]])
        guard_violations=triangle_check(self.base,v,self.guard_triangles)
        inversions=int(np.sum((np.sum(base_cross*base_cross,axis=1)>1e-24)&(np.sum(base_cross*fit_cross,axis=1)<0)))
        eye_crossings=len(crossings(v,self.eye_triangles,self.tt,True)) if self.eye_triangles is not None else None
        return {'eye_skin_proper_crossing_pairs':eye_crossings,
                'surface_mean_cm':surface*self.scale,'raw_surface_symmetric_mean_cm':float(.5*(raw_distance.mean()+reverse.mean())*self.scale),
                'surface_model':self.source.get('surface_interpolation','linear'),'normal_one_minus_cosine':normal,'boundary_candidate_mean_cm':boundary*self.scale,
                'projected_contour_normalized':silhouettes,'official_curve_surface_distances':regions,
                'triangle_guard_violations':guard_violations,'orientation_inversions':inversions,
                'quad_diagonal_protection':bool(self.head.get('official_quads')),
                'flipped_triangles':guard_violations,'legacy_flipped_triangles_includes_guard_margin':True,
                'edge_ratio_min':float(ratio.min()),'edge_ratio_max':float(ratio.max()),
                'score':surface+.01*normal+.25*boundary+.2*np.mean(list(silhouettes.values())),
                'semantic_correspondence_complete':False,'visual_acceptance_passed':False}

    def solve(self,profile,checkpoint=None):
        iterations,regularization,boundary_weight=PROFILES[profile];v=self.base.copy();history=[]
        reg=regularization*(self.lap.T@self.lap)
        if self.source.get('eye_sampling_regularization') and self.eye_surfaces:
            # A dense MH rim must not introduce displacement frequencies absent
            # from the coarser source rim. Scale differential regularization
            # by the topology sampling ratio, not a person's coordinates.
            eye_lap=self.lap[self.eye_surfaces[0][0]]
            reg=reg+regularization*(self.eye_sampling_factor-1)*(eye_lap.T@eye_lap)
        arap_weight=.5 if self.source.get('strain_model')=='arap' else 0.
        if arap_weight:reg=reg+arap_weight*(self.inc.T@self.inc)
        for step in range(iterations):
            vn,_=normals(v,self.tt);hits,source_normals,d=self.query_fit_surface(v,self.surface_ids,vn)
            ids=self.surface_ids[np.isfinite(d)&(d<.04)]; targets=hits[np.isfinite(d)&(d<.04)]
            weights=np.full(len(v),.002); weights[self.fixed]=1e5
            rhs=np.zeros_like(v); weights[ids]+=1.;rhs[ids]+=targets-v[ids]
            np.add.at(weights,self.boundary_ids,boundary_weight)
            np.add.at(rhs,self.boundary_ids,boundary_weight*(self.boundary_points-v[self.boundary_ids]))
            if arap_weight:rhs+=arap_weight*(self.inc.T@(arap_edges(self.base,v,self.edges)-self.inc@v))
            step_reg=reg
            if self.source.get('normal_objective')=='tangent-edges':
                accepted=np.zeros(len(v),bool);accepted[ids]=True
                active=np.all(accepted[self.edges],axis=1)
                normals_field=np.zeros_like(v);normals_field[self.surface_ids]=source_normals
                edge_inc=self.inc[active];edge_ids=self.edges[active]
                normal_weight=1.
                step_reg=reg+normal_weight*(edge_inc.T@edge_inc)
                rhs+=normal_weight*(edge_inc.T@(tangent_edges(v,edge_ids,normals_field)-edge_inc@v))
            if self.source.get('eye_tangent_refinement') and self.eye_surfaces:
                exterior_ids,exterior=self.eye_surfaces[0]
                _,guides,_=exterior.query(v[exterior_ids])
                active=np.all(np.isin(self.edges,exterior_ids),axis=1)
                edge_inc=self.inc[active];edge_ids=self.edges[active]
                field=np.zeros_like(v);field[exterior_ids]=guides
                weight=2.
                step_reg=step_reg+weight*(edge_inc.T@edge_inc)
                rhs+=weight*(edge_inc.T@(tangent_edges(v,edge_ids,field)-edge_inc@v))
            # Incremental smooth deformation reduces residual bias left by a
            # fixed-base differential penalty while preserving each local step.
            matrix=(step_reg+sparse.diags(weights)).tocsr();inv=1/matrix.diagonal()
            pre=LinearOperator(matrix.shape,matvec=lambda x:inv*x)
            delta=np.empty_like(v)
            for axis in range(3):
                delta[:,axis],status=cg(matrix,rhs[:,axis],M=pre,rtol=1e-7,maxiter=1200)
                if status:raise ValueError('AdaptiveLinearSolveIncomplete')
            delta[self.fixed]=0
            trial,factors=local_guard(self.base,v,delta,self.guard_triangles,self.guard_edges,self.guard_lengths,self.eye_triangles,self.tt,
                                     .05 if self.source.get('eye_clearance_guard') else 0.)
            v=trial;history.append({'iteration':step,'minimum_local_step':float(factors.min()),'limited_vertices':int((factors<1).sum()),
                'matched':len(ids),'max_delta_cm':float(np.linalg.norm(delta*factors[:,None],axis=1).max()*self.scale)})
            if checkpoint:checkpoint(history[-1])
            if history[-1]['max_delta_cm']<1e-7:break
        return v,history
