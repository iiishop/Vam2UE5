import contextlib, io, json, tempfile, unittest
from pathlib import Path
import numpy as np
from prepare_hair_native_roots import main

class NativeRootImport(unittest.TestCase):
    def fixture(self, root):
        tr=np.eye(4);tr[0,3]=.0088
        p=np.array([[0,0,0],[0,.02,0]],dtype='<f4')
        p.tofile(root/'hair-provider.f32')
        (p+np.array([.0088,0,0],dtype='<f4')).tofile(root/'hair-particles.f32')
        manifest={'physics':'False','hairs':[{'path':'Actor:Example','prefix':'hair','provider_count':2,'source_segments':2,'root_to_scalp':[0],'root_transforms':[tr.reshape(-1).tolist()]}]}
        prepared={'identity':'before','body_contract':'body','groups':[{'source_path':'Example.vab','kind':'native_full','curves':[{'source_curve':0,'anchor_index':0,'points':[[0,0,0],[0,0,2]]}]}],'catalog':{'assets':[{'source_path':'Example.vab','source':{'dynamic':{'hair':{'vertices':p.tolist(),'root_to_scalp':[0]}}}}]}}
        contract={'contract_id':'body','bones':[{'name':'head','world_bind':np.eye(4).tolist()}]}
        body={'bones':[{'id':'head','worldMatrix':np.eye(4).reshape(-1).tolist()}]}
        for name,value in [('hair-rest.json',manifest),('prepared.json',prepared),('contract.json',contract),('hair-rest-body.json',body)]:
            (root/name).write_text(json.dumps(value))

    def test_verified_transform_preserves_source_ids_and_records_fit_discrepancy(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            with contextlib.redirect_stdout(io.StringIO()):main(root,root/'prepared.json',root/'contract.json',root/'out.json')
            d=json.loads((root/'out.json').read_text())
            self.assertAlmostEqual(d['groups'][0]['curves'][0]['points'][0][1],.88)
            self.assertGreater(d['native_root_fit']['before']['resources'][0]['max_cm'],.8)
            self.assertLess(d['native_root_fit']['reconstruction'][0]['particle_reconstruction_max_cm'],.001)
            self.assertEqual(d['groups'][0]['curves'][0]['source_curve'],0)

    def test_unverified_runtime_particles_rejected(self):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp);self.fixture(root)
            np.array([[0,0,0],[0,.02,0]],dtype='<f4').tofile(root/'hair-particles.f32')
            with self.assertRaisesRegex(AssertionError,'Native root reconstruction failed'):
                main(root,root/'prepared.json',root/'contract.json',root/'out.json')
            self.assertFalse((root/'out.json').exists())

if __name__=='__main__':unittest.main()
