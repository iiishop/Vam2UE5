"""Small, independently authored geometry fixtures; no redistributed VaM assets."""
import copy
import json
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0,str(Path(__file__).parents[1]/'Scripts'))
import vam_decode as d
from vam_unity import validate_skin

def s(value):
    raw=value.encode();n=len(raw);prefix=[]
    while n>127:prefix.append((n&127)|128);n>>=7
    return bytes(prefix+[n])+raw
def i(n):return struct.pack('<i',n)
def floats(*v):return struct.pack('<'+'f'*len(v),*v)
def header(name,version='1.0'):return s(name)+s(version)
def array(fmt,rows):return i(len(rows))+b''.join(struct.pack('<'+fmt,*r)for r in rows)

def triangle():
    return {'vertices':[[0.,0.,0.],[1.,0.,0.],[0.,1.,0.]],'uv':[[0.,0.],[1.,0.],[0.,1.]],
            'materials':['Test region'],'polygons':[{'materialNum':0,'vertices':[0,1,2]}],
            'uv_polygons':[{'materialNum':0,'vertices':[0,1,2]}],'uv_map':[]}

def vab_fixture():
    m=triangle();poly=i(0)+i(3)+i(0)+i(1)+i(2)
    mesh=header('DAZMesh')+s('node')+s('scene')+s('geometry')+s('instance')
    mesh+=array('fff',m['vertices'])+i(1)+s('Test region')+i(1)+poly+poly+array('ff',m['uv'])+i(0)
    wrap=header('DAZSkinWrap')+s('wrap')+header('DAZSkinWrapStore')+array('iiiiffffff',[[0,0,1,2,0,0,0,1,0,0]]*3)
    material=header('MaterialOptions')+s('options')+array('i',[[0]])
    return header('DynamicStore')+mesh+wrap+material+b'\0',{'itemType':'ClothingFemale'}, {'components':[{'type':x}for x in ['DAZMesh','DAZSkinWrap','DAZSkinWrapMaterialOptions']]}

def hair_fixture(version):
    v=[[0,0,0],[0,0,1],[1,0,0],[1,0,1]]
    raw=header('DynamicStore')+b'\1'+header('RuntimeHairGeometryCreator',version)+s('scalp')+i(2)+floats(1)+s('mask')+i(2)+b'\1\1'
    raw+=i(2)+i(0)+array('fff',v[:2])+i(1)+array('fff',v[2:])+array('i',[[0],[1],[2]])+array('fff',v)
    if version=='1.1':raw+=array('f',[[1]]*4)
    raw+=array('i',[[0],[1]])+i(0)
    return raw,{'itemType':'HairFemale'},{'components':[]}

class DecodeTests(unittest.TestCase):
    def test_hair_credit_trailer_is_narrow_and_preserved(self):
        import hashlib
        raw,vam,vaj=hair_fixture('1.1')
        trailer=b'Thanks:"Example"+"hair_version: example.1"\r\n'
        result=d.decode_vab(raw+trailer,vam,vaj)
        self.assertEqual(result['sha256'],hashlib.sha256(raw+trailer).hexdigest())
        self.assertEqual(result['metadata_compatibility'][0]['byte_offset'],len(raw))
        self.assertEqual(result['metadata_compatibility'][0]['raw_trailer'],trailer.decode())
        for bad in (raw+b'unknown',raw+trailer+b'\0',raw[:-1]+trailer):
            with self.assertRaises(d.DecodeError):d.decode_vab(bad,vam,vaj)
        raw,vam,vaj=vab_fixture()
        with self.assertRaises(d.DecodeError):d.decode_vab(raw+trailer,vam,vaj)

    def test_vmb_layout_length_indices_and_finite(self):
        raw=i(2)+struct.pack('<ifffifff',0,.1,.2,.3,2,0,0,1)
        meta={'numDeltas':'2','unknown':{'keep':True}}
        a=d.decode_vmb(raw,meta,3);self.assertEqual(a,d.decode_vmb(raw,meta,3));self.assertEqual(a['parameters'],meta)
        for bad in [raw[:-1],raw+b'\0',i(3)+raw[4:],i(-1)]:
            with self.assertRaises(d.DecodeError):d.decode_vmb(bad,meta,3)
        for index,value in [(3,1),(-1,1),(0,float('nan')),(0,float('inf'))]:
            with self.assertRaises(d.DecodeError):d.decode_vmb(i(1)+struct.pack('<ifff',index,value,0,0),{'numDeltas':1},3)
        with self.assertRaises(d.DecodeError):d.decode_vmb(i(2)+struct.pack('<ifff',0,0,0,0)*2,meta,3)

    def test_vab_full_read_and_repeat(self):
        raw,vam,vaj=vab_fixture();a=d.decode_vab(raw,vam,vaj)
        self.assertEqual(a,d.decode_vab(raw,vam,vaj));self.assertEqual(len(a['meshes'][0]['vertices']),3)
        self.assertEqual(a['material_options'][0]['slots'],[0])
        self.assertEqual(len(a['wraps'][0]['vertices']),3)

    def test_vab_every_truncation_and_trailing(self):
        raw,vam,vaj=vab_fixture()
        for n in range(len(raw)):
            with self.subTest(length=n),self.assertRaises(d.DecodeError):d.decode_vab(raw[:n],vam,vaj)
        with self.assertRaises(d.DecodeError):d.decode_vab(raw+b'\0',vam,vaj)

    def test_unknown_layout_stops(self):
        raw,vam,vaj=vab_fixture()
        with self.assertRaisesRegex(d.DecodeError,'unsupported_version'):d.decode_vab(raw.replace(b'1.0',b'9.0',1),vam,vaj)
        with self.assertRaisesRegex(d.DecodeError,'unknown_component'):d.decode_vab(raw,vam,{'components':[{'type':'FutureMesh'}]})

    def test_hair_both_versions(self):
        for version in ['1.0','1.1']:
            raw,vam,vaj=hair_fixture(version);result=d.decode_vab(raw,vam,vaj)
            hair=result['dynamic']['hair'];self.assertEqual(hair['version'],version);self.assertEqual(len(hair['vertices']),4)
            with self.assertRaises(d.DecodeError):d.decode_vab(raw[:-1],vam,vaj)

    def test_geometry_uv_material_and_mapping_validation(self):
        m=triangle();m['uv'].append([.1,.2]);m['uv_map']=[{'fromvert':0,'tovert':3,'polyindex':0}];m['uv_polygons'][0]['vertices']=[3,1,2]
        p=d.preview_mesh(m,'fixture',{'source':'synthetic'})
        self.assertEqual(p['converted_to_source_vertex'],[0,1,2,0]);self.assertEqual(p['sections'],[[2,1,3]])
        self.assertEqual(p['triangle_to_source_polygon'],[[0]])
        for mutate in [lambda x:x['uv_polygons'][0].update(vertices=[4,1,2]),lambda x:x['polygons'][0].update(materialNum=2),lambda x:x['vertices'][0].__setitem__(0,float('nan')),lambda x:x['uv_map'][0].update(fromvert=2)]:
            bad=copy.deepcopy(m);mutate(bad)
            with self.assertRaises(d.DecodeError):d.validate_mesh(bad)

    def test_coordinate_roundtrip(self):
        for v in [[0,0,0],[1,-2,3],[.000001,-3.45678,987.654321]]:
            for a,b in zip(v,d.from_ue(d.to_ue(v))):self.assertAlmostEqual(a,b,places=10)
        self.assertEqual(d.to_ue([0,0,1]),[100,0,0]);self.assertEqual(d.to_ue([0,1,0]),[0,0,100])
        self.assertEqual(d.uv_to_ue(d.uv_to_ue([.125,.75])),[.125,.75])

    def test_skin_reference_weight_index_and_nonfinite(self):
        skin={'_numBones':1,'_hasGeneralWeights':False,'nodes':[{'name':'root','weights':[{'vertex':0,'xweight':1.,'yweight':1.,'zweight':1.}], 'generalWeights':[],'fullyWeightedVertices':[]}]}
        validate_skin(skin,triangle(),[{'name':'root'}])
        for mutate in [lambda x:x['nodes'][0].update(name='missing'),lambda x:x['nodes'][0]['weights'][0].update(vertex=9),lambda x:x['nodes'][0]['weights'][0].update(xweight=-.1),lambda x:x['nodes'][0]['weights'][0].update(yweight=float('nan'))]:
            bad=copy.deepcopy(skin);mutate(bad)
            with self.assertRaises(d.DecodeError):validate_skin(bad,triangle(),[{'name':'root'}])

if __name__=='__main__':unittest.main()
