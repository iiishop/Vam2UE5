"""Offline experimental semantic-constrained residual solver; not an Epic API.

All profiles share topology annotations, stages, objective and acceptance rules.
No source name, character identifier, camera landmark or fixed-space repair.
"""
import numpy as np
from scipy import sparse
from scipy.spatial import cKDTree
from scipy.sparse.linalg import lsqr
from vam_face_surface import closest, normals
from vam_face_semantics import FidelityError, geometry, validate_map

VERSION = 'face-fidelity/1'
VIEWS = ((0, 0), (-45, 0), (45, 0), (-90, 0), (90, 0), (0, -20), (0, 20))
PROFILES = {
    'Conservative': {'surface': .5, 'semantic': 30., 'laplacian': 20., 'strain': 8.},
    'Balanced': {'surface': 1., 'semantic': 40., 'laplacian': 10., 'strain': 5.},
    'SemanticStrong': {'surface': .5, 'semantic': 80., 'laplacian': 10., 'strain': 5.},
    'SurfaceStrong': {'surface': 2., 'semantic': 40., 'laplacian': 8., 'strain': 5.},
}


def view_matrix(yaw, pitch):
    a, b = np.radians([yaw, pitch])
    right = np.array([np.cos(a), -np.sin(a), 0.])
    forward = np.array([np.sin(a)*np.cos(b), np.cos(a)*np.cos(b), np.sin(b)])
    return np.stack((right, np.cross(right, forward), forward))


def contour(v, triangles, view):
    """Occluding/boundary edge samples, not a convex hull approximation."""
    p = v @ view.T
    signs = np.sign(np.cross(p[triangles[:, 1]]-p[triangles[:, 0]], p[triangles[:, 2]]-p[triangles[:, 0]])[:, 2])
    edges = {}
    for tri, sign in zip(triangles, signs):
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            edges.setdefault(tuple(sorted((int(a), int(b)))), []).append(sign)
    ids = np.array([e for e, s in edges.items() if len(s) == 1 or min(s) < max(s)], int)
    if not len(ids): raise FidelityError('EmptySilhouette')
    # Metric is sampled projected contour distance, including open boundaries;
    # it is not an occlusion-aware raster silhouette or visual score.
    return ids, p[ids, :2].mean(axis=1)


class Surface:
    def __init__(self, vertices, triangles):
        self.triangles = vertices[triangles]
        _, self.normals = normals(vertices, triangles)
        self.tree = cKDTree(self.triangles.mean(axis=1))

    def query(self, points, vertex_normals=None):
        k = min(32, len(self.triangles))
        _, indices = self.tree.query(points, k=k)
        indices = np.asarray(indices).reshape(len(points), k)
        hits = closest(np.repeat(points, k, axis=0), self.triangles[indices.ravel()]).reshape(len(points), k, 3)
        d = np.linalg.norm(hits-points[:, None, :], axis=2)
        if vertex_normals is not None:
            d[np.sum(self.normals[indices]*vertex_normals[:, None, :], axis=2) < .65] = np.inf
        # Resolve shared-edge/vertex ties by triangle identity, not the order
        # returned by a spatial tree (which can change under rigid rotation).
        minimum=np.min(d,axis=1)
        tied=np.isfinite(d)&(d<=minimum[:,None]+1e-12)
        pick=np.argmin(np.where(tied,indices,np.iinfo(np.int64).max),axis=1);row=np.arange(len(points))
        return hits[row, pick], self.normals[indices[row, pick]], d[row, pick]


def operators(triangles, count):
    edges = np.unique(np.sort(np.concatenate([triangles[:, [0, 1]], triangles[:, [1, 2]], triangles[:, [2, 0]]]), axis=1), axis=0)
    row = np.repeat(np.arange(len(edges)), 2)
    incidence = sparse.csr_matrix((np.tile([-1., 1.], len(edges)), (row, edges.ravel())), shape=(len(edges), count))
    adj = abs(incidence).T @ abs(incidence); adj.setdiag(0); adj.eliminate_zeros()
    degree = np.asarray(adj.sum(axis=1)).ravel()
    lap = sparse.diags((degree > 0).astype(float))-sparse.diags(1/np.maximum(degree, 1))@adj
    return edges, incidence, lap


def triangle_check(base, proposed, triangles):
    old = np.cross(base[triangles[:, 1]]-base[triangles[:, 0]], base[triangles[:, 2]]-base[triangles[:, 0]])
    new = np.cross(proposed[triangles[:, 1]]-proposed[triangles[:, 0]], proposed[triangles[:, 2]]-proposed[triangles[:, 0]])
    area = np.linalg.norm(old, axis=1); valid = area > 1e-12
    return int(np.count_nonzero((np.sum(old*new, axis=1) <= .05*area**2) & valid))


class Problem:
    def __init__(self, source, target, semantic_map):
        self.resolved = validate_map(semantic_map, source, target)
        sv, self.st = geometry(source); base, self.tt = geometry(target)
        self.sf = self.st[semantic_map['source_face_triangles']]
        self.tf = self.tt[semantic_map['target_face_triangles']]
        self.si = np.unique(self.sf); self.ti = np.unique(self.tf)
        self.origin = sv[self.si].mean(axis=0)
        self.scale = float(np.linalg.norm(np.ptp(sv[self.si], axis=0)))
        if self.scale <= 1e-8: raise FidelityError('DegenerateFace')
        self.source = (sv-self.origin)/self.scale
        self.base = (base-self.origin)/self.scale
        self.surface = Surface(self.source, self.sf)
        self.edges, self.incidence, self.lap = operators(self.tt, len(base))
        self.rest_lengths = np.linalg.norm(self.incidence@self.base, axis=1)
        self.fixed = np.setdiff1d(np.arange(len(base)), self.ti)
        self.semantic_ids = np.unique(np.concatenate([m.indices for _, m in self.resolved.values()]))
        # Keep all annotated loops and two topological rings out of generic ICP.
        protected = set(self.semantic_ids.tolist())
        for _ in range(2):
            protected.update(self.edges[np.any(np.isin(self.edges, list(protected)), axis=1)].ravel().tolist())
        self.surface_ids = np.setdiff1d(self.ti, list(protected))
        self.source_contours = {v: contour(self.source, self.sf, view_matrix(*v))[1] for v in VIEWS}

    def metrics(self, current):
        hits, ns, dist = self.surface.query(current[self.ti])
        vn, _ = normals(current, self.tt)
        reverse = Surface(current, self.tf).query(self.source[self.si])[2]
        semantics = {name: float(np.sqrt(np.mean(np.sum((a@self.source-b@current)**2, axis=1))))
                     for name, (a, b) in self.resolved.items()}
        silhouettes = {}
        for view in VIEWS:
            q = contour(current, self.tf, view_matrix(*view))[1]; p = self.source_contours[view]
            silhouettes[str(view)] = float(.5*(cKDTree(p).query(q)[0].mean()+cKDTree(q).query(p)[0].mean()))
        surface = float(.5*(dist.mean()+reverse.mean()))
        normal = float(np.mean(1-np.clip(np.sum(vn[self.ti]*ns, axis=1), -1, 1)))
        lengths = np.linalg.norm(self.incidence@current, axis=1)
        strain = np.abs(np.divide(lengths, self.rest_lengths, out=np.ones_like(lengths), where=self.rest_lengths > 1e-10)-1)
        score = surface + .01*normal + 2*np.mean(list(semantics.values())) + np.mean(list(silhouettes.values()))
        return {'surface_symmetric_mean_normalized': surface, 'surface_symmetric_mean_cm': surface*self.scale,
                'normal_one_minus_cosine': normal, 'semantics_normalized': semantics,
                'projected_contour_normalized': silhouettes, 'max_edge_strain': float(strain.max()),
                'flipped_triangles': triangle_check(self.base, current, self.tt), 'score': float(score),
                'visual_acceptance_passed': False}

    def solve(self, profile, check_cancel=None):
        cfg = PROFILES[profile]; current = self.base.copy(); history = []
        count = len(current); eye = sparse.eye(count, format='csr')
        # Coarse stage only uses broad topology annotations; then facial
        # semantics; surface is allowed only after both preceding stages.
        for stage in ('coarse', 'semantic', 'surface'):
            for iteration in range(4):
                if check_cancel: check_cancel()
                matrices, targets = [], []
                def add(matrix, target, weight):
                    w = np.sqrt(weight); matrices.append(matrix*w); targets.append(target*w)
                add(self.lap, self.lap@self.base, cfg['laplacian'])
                # Edge-vector regularization resists strain/bending; no claim
                # of full rotationally invariant ARAP is made.
                add(self.incidence, self.incidence@self.base, cfg['strain'])
                add(eye, self.base, .01)
                if len(self.fixed): add(eye[self.fixed], self.base[self.fixed], 1e6)
                for name, (a, b) in self.resolved.items():
                    exact = np.linalg.norm(a@self.source-b@self.base, axis=1) < 1e-8
                    if exact.any(): add(b[exact], (a@self.source)[exact], 1e6)
                    if stage == 'coarse' and not any(n in name for n in ('jaw', 'chin', 'cheek', 'silhouette', 'ear')): continue
                    add(b, a@self.source, cfg['semantic']/b.shape[0])
                if stage == 'surface' and len(self.surface_ids):
                    vn, _ = normals(current, self.tt)
                    ids = self.surface_ids
                    hits, ns, dist = self.surface.query(current[ids], vn[ids])
                    valid = dist < .04; ids = ids[valid]
                    add(eye[ids], hits[valid], cfg['surface'])
                matrix = sparse.kron(sparse.vstack(matrices), sparse.eye(3), format='csr')
                rhs = np.vstack(targets).ravel()
                extra, extra_rhs = [], []
                if stage == 'surface':
                    # Tangent edge constraints penalize normal disagreement.
                    edges = self.edges[np.all(np.isin(self.edges, self.surface_ids), axis=1)]
                    if len(edges):
                        _, ns, distances = self.surface.query(current[edges].mean(axis=1))
                        valid = distances < .04; edges, ns = edges[valid], ns[valid]
                        rows = np.repeat(np.arange(len(edges)), 6)
                        cols = (edges[:, :, None]*3+np.arange(3)).reshape(-1)
                        values = np.stack([-ns, ns], axis=1).ravel()
                        extra.append(sparse.csr_matrix((values, (rows, cols)), shape=(len(edges), 3*count))*.2)
                        extra_rhs.append(np.zeros(len(edges)))
                    # Constrain the projected contour in all seven fixed views.
                    # Exclude protected semantic vertices: silhouette must not
                    # replace an eyelid/nose/lip correspondence.
                    for view in VIEWS:
                        rotation = view_matrix(*view)
                        edges, pixels = contour(current, self.tf, rotation)
                        valid = ~np.any(np.isin(edges, self.semantic_ids), axis=1)
                        edges, pixels = edges[valid], pixels[valid]
                        if not len(edges): continue
                        reference = self.source_contours[view]
                        distance, index = cKDTree(reference).query(pixels)
                        valid = distance < .04; edges, index = edges[valid], index[valid]
                        for axis in range(2):
                            rows = np.repeat(np.arange(len(edges)), 6)
                            cols = (edges[:, :, None]*3+np.arange(3)).reshape(-1)
                            values = np.tile(np.tile(rotation[axis]*.5, 2), len(edges))
                            extra.append(sparse.csr_matrix((values, (rows, cols)), shape=(len(edges), 3*count))*.2)
                            extra_rhs.append(reference[index, axis]*.2)
                if extra:
                    matrix = sparse.vstack([matrix]+extra).tocsr(); rhs = np.concatenate([rhs]+extra_rhs)
                solution = lsqr(matrix, rhs, atol=1e-8, btol=1e-8, iter_lim=2400)
                if solution[1] not in (0, 1, 2): raise FidelityError('ResidualLinearSolveIncomplete')
                proposed = solution[0].reshape(count, 3); proposed[self.fixed] = self.base[self.fixed]
                alpha = 1.
                while alpha >= 1/256:
                    trial = current+alpha*(proposed-current)
                    lengths = np.linalg.norm(self.incidence@trial, axis=1)
                    ratio = np.divide(lengths, self.rest_lengths, out=np.ones_like(lengths), where=self.rest_lengths > 1e-10)
                    if triangle_check(self.base, trial, self.tt) == 0 and ratio.min() > .5 and ratio.max() < 1.5: break
                    alpha *= .5
                if alpha < 1/256:
                    history.append({'stage': stage, 'iteration': iteration, 'state': 'GeometricGuardRejected'})
                    break
                delta = float(np.linalg.norm(trial-current, axis=1).max()); current = trial
                history.append({'stage': stage, 'iteration': iteration, 'step': alpha, 'max_step_normalized': delta})
                if delta < 1e-7: break
        return current, history

    def candidates(self, checkpoint=None, check_cancel=None):
        baseline = self.metrics(self.base); records = {'Baseline': {'metrics': baseline, 'eligible': True}}
        winner, best, vertices = 'Baseline', baseline['score'], self.base.copy()
        for profile in PROFILES:
            try:
                candidate, history = self.solve(profile, check_cancel); metric = self.metrics(candidate)
                # An average may not hide a worsened eye/nose/lip semantic.
                eligible = metric['flipped_triangles'] == 0 and all(
                    value <= baseline['semantics_normalized'][name]*1.05+1e-5
                    for name, value in metric['semantics_normalized'].items())
                records[profile] = {'metrics': metric, 'history': history, 'eligible': eligible}
                if eligible and metric['score'] < best:
                    winner, best, vertices = profile, metric['score'], candidate.copy()
            except FidelityError as exc:
                records[profile] = {'eligible': False, 'error': str(exc)}
            if checkpoint: checkpoint(profile, records[profile])
        return vertices*self.scale+self.origin, {'version': VERSION, 'chosen_solver_profile': winner,
            'profiles': PROFILES, 'candidates': records, 'normalization_cm': self.scale,
            'views': VIEWS, 'visual_acceptance_passed': False}
