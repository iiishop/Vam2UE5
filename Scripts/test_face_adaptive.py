import unittest
import tempfile,json
from pathlib import Path
import numpy as np
from vam_face_adaptive import local_guard,segments,arap_edges
from vam_face_fidelity import operators,triangle_check,Surface
from vam_face_phong import evaluate,PhongSurface
from vam_face_adaptive_verify import attach_task
from vam_face_adaptive import PROFILES,tangent_edges
from vam_face_fidelity_finish import fresh_lifecycle,reusable_rig_exports
from vam_face_adaptive_cli import run


class AdaptiveTests(unittest.TestCase):
    def test_candidate_cache_requires_matching_algorithm_lock(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);request=root/'request.json'
            request.write_text(json.dumps({'output':str(root)}),encoding='utf8')
            (root/'Balanced.json').write_text('{}',encoding='utf8')
            with self.assertRaisesRegex(ValueError,'LegacyCandidateCacheWithoutAlgorithmLock'):run(request)
            (root/'algorithm-lock.json').write_text('{}',encoding='utf8')
            with self.assertRaisesRegex(ValueError,'AlgorithmChanged'):run(request)

    def test_resume_rejects_changed_rig_capture_or_character(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory)
            face={'vertices':[[0.,0,0],[1,0,0],[0,1,0]],'triangles':[0,1,2]}
            for name in ('RigSnapshot','RigReload'):
                p=root/name;p.mkdir()
                (p/'reload.json').write_text(json.dumps({'character':'/Game/A.A','character_sha256':'rig','source_valid':True,'full_rig':True}),encoding='utf8')
                for f in ('actual-face.json','actual-head.json'):(p/f).write_text(json.dumps(face),encoding='utf8')
            self.assertTrue(reusable_rig_exports(root,'/Game/A','rig'))
            self.assertFalse(reusable_rig_exports(root,'/Game/B','rig'))
            self.assertFalse(reusable_rig_exports(root,'/Game/A','changed'))
            face['vertices'][0][0]=.1
            for f in ('actual-face.json','actual-head.json'):(root/'RigReload'/f).write_text(json.dumps(face),encoding='utf8')
            self.assertFalse(reusable_rig_exports(root,'/Game/A','rig'))

    def test_tangent_objective_is_rotation_equivariant(self):
        v=np.array([[0.,0,0],[1,0,.2],[0,1,-.1]])
        e=np.array([[0,1],[1,2],[0,2]]);n=np.tile([0.,0,1],(3,1))
        r=np.array([[0.,-1,0],[0,0,-1],[1,0,0]])
        target=tangent_edges(v,e,n)
        np.testing.assert_allclose(target[:,2],0,atol=1e-12)
        np.testing.assert_allclose(tangent_edges(v@r,e,n@r),target@r,atol=1e-12)

    def test_new_character_does_not_inherit_delivery_receipts(self):
        source={'inputs':{'source_ir':{'sha256':'locked'}},'cooked_verification':{'passed':True},
                'assembly_attempts':[{'path':'old'}],'blueprint':'old','fidelity_post_rig':{'sha256':'old'}}
        fresh=fresh_lifecycle(source)
        self.assertEqual(fresh,{'inputs':source['inputs']})
        fresh['inputs']['source_ir']['sha256']='new'
        self.assertEqual(source['inputs']['source_ir']['sha256'],'locked')

    def test_official_dispatch_rejects_partial_or_mismatched_candidate(self):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);out=root/'experiment';inputs=root/'inputs';out.mkdir();inputs.mkdir()
            def write(path,value):path.write_text(json.dumps(value),encoding='utf8')
            write(inputs/'task.json',{});write(out/'input-lock.json',{})
            write(out/'metrics.json',{'state':'Draft','candidates':{},'chosen_solver_profile':'Balanced'})
            with self.assertRaisesRegex(ValueError,'CandidateSolveIncomplete'):attach_task(out,inputs)
            write(out/'metrics.json',{'state':'Draft','candidates':{key:{} for key in PROFILES},'chosen_solver_profile':'Balanced'})
            write(out/'template.json',{'fidelity_report':{'chosen_solver_profile':'Conservative'}})
            with self.assertRaisesRegex(ValueError,'CandidateTemplateMismatch'):attach_task(out,inputs)
            self.assertFalse((out/'task.json').exists())

    def test_surface_ties_follow_triangle_identity_after_rotation(self):
        v=np.array([[0.,0,0],[1,0,0],[0,1,0],[0,0,1]])
        t=np.array([[0,1,2],[0,1,3]]);p=np.array([[.4,0,0]])
        a=Surface(v,t);_,n,_=a.query(p)
        np.testing.assert_allclose(n[0],a.normals[0])
        c=np.cos(.7);s=np.sin(.7);r=np.array([[c,-s,0],[s,c,0],[0,0,1]])
        b=Surface(v@r.T,t);_,rotated,_=b.query(p@r.T)
        np.testing.assert_allclose(rotated@r,n,atol=1e-12)

    def test_arap_accepts_rigid_rotation_without_residual(self):
        base=np.array([[0.,0,0],[1,0,0],[0,1,0],[0,0,1]])
        edges=np.array([[0,1],[0,2],[0,3],[1,2],[1,3],[2,3]])
        r=np.array([[0.,-1,0],[1,0,0],[0,0,1]])
        current=base@r.T+[3,4,5]
        np.testing.assert_allclose(arap_edges(base,current,edges),current[edges[:,1]]-current[edges[:,0]],atol=1e-12)

    def test_local_inversion_does_not_stop_unrelated_patch(self):
        base=np.array([[0.,0,0],[1,0,0],[0,1,0],[3,0,0],[4,0,0],[3,1,0]])
        t=np.array([[0,1,2],[3,4,5]]);edges,inc,_=operators(t,6);lengths=np.linalg.norm(inc@base,axis=1)
        delta=np.zeros_like(base);delta[2,1]=-2;delta[3:,2]=.2
        result,scale=local_guard(base,base,delta,t,edges,lengths)
        self.assertEqual(triangle_check(base,result,t),0)
        np.testing.assert_allclose(result[3:],base[3:]+delta[3:]);self.assertLess(scale[2],1)

    def test_edge_barycentric_projection_uses_actual_segment(self):
        hit,index,fraction=segments(np.array([[.3,1.,0]]),np.array([[0.,0,0]]),np.array([[1.,0,0]]))
        np.testing.assert_allclose(hit,[[.3,0,0]]);np.testing.assert_allclose(fraction,[.3])

    def test_point_normal_surface_preserves_vertices_and_planar_geometry(self):
        v=np.array([[0.,0,0],[1,0,0],[0,1,0]]);t=np.array([[0,1,2]])
        surface=PhongSurface(v,t)
        hits,_,distance=surface.query(np.array([[.2,.3,2.]]))
        np.testing.assert_allclose(hits,[[.2,.3,0]],atol=1e-10)
        np.testing.assert_allclose(distance,[2.])
        n=np.array([[[0.,0,1],[.2,0,.98],[0,.2,.98]]]);n/=np.linalg.norm(n,axis=2)[:,:,None]
        for corner in range(3):
            w=np.eye(3)[corner:corner+1];p,_,_=evaluate(v[None,:,:],n,w)
            np.testing.assert_allclose(p,v[corner:corner+1])


if __name__=='__main__':unittest.main()
