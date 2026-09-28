import unittest
from vam_glute_corrective_source import recover, source_driver_weight

def fixture():
    mesh={'vertices':[[0.,0.,0.],[1.,0.,0.],[0.,0.,0.],[.5,0.,0.]],'polygons':[{'vertices':[0,1,2,3]}]}
    params={'targetMesh':{'m_PathID':1},'graftMesh':{'m_PathID':2},'graftMethod':1,'numGraftBaseVertices':2,'startGraftVertIndex':2,'_graftWeights':[0, .5],'_graftIsFreeVert':[False,True],'_graftXFactor':1,'_graftYFactor':1,'_graftZFactor':1}
    records=[{'class':'DAZMergedMesh','object':'3','parameters':params,'mesh':mesh},
        {'kind':'unity_mesh','object':'1','mesh':{'vertices':mesh['vertices'][:2]}},
        {'kind':'unity_mesh','object':'2','parameters':{'meshGraft':{'vertexPairs':[{'vertexNum':0,'graftToVertexNum':0}]}}},
        {'kind':'skin','parameters':{'dazMesh':{'m_PathID':3},'_useGeneralWeights':False,'nodes':[],'bulgeScale':.0015}}]
    source={'version':1,'target':'test','source_hashes':{'a':'123'},'deltas':[[0,1.,2.,3.]],'reason':'fixture'}
    family={'glute_corrective':{'source_target':'test'},'glute_structure':{'left_thigh':'L','right_thigh':'R'},'glute_corrective_source':source}
    return {'source_hashes':{'a':'123'},'records':records},family

class CorrectiveSourceTests(unittest.TestCase):
    def test_unsupported(self):
        with self.assertRaisesRegex(ValueError,'Unsupported family'):recover({},'missing',{})
    def test_missing_library(self):
        self.assertEqual(recover({},'missing',{'glute_corrective':{}})['mode'],'procedural')
    def test_retained_chain_replays_boundary_offline(self):
        ir,family=fixture();s=recover(ir,'missing',family)
        self.assertEqual(s['raw_deltas'],[[0,1.,2.,3.]])
        d={v:xyz for v,*xyz in s['deltas']}
        for actual,expected in zip(d[2],[1,2,3]):self.assertAlmostEqual(actual,expected)
        for actual,expected in zip(d[3],[.5,1,1.5]):self.assertAlmostEqual(actual,expected)
        self.assertEqual(s['version'],2)
        family['glute_corrective_source']=s
        self.assertEqual(recover(ir,'missing',family)['deltas'],s['deltas'])
    def test_retained_digest_rejected(self):
        ir,family=fixture();ir['source_hashes']['a']='different'
        self.assertEqual(recover(ir,'missing',family)['mode'],'procedural')
    def test_missing_topology_is_not_silent_vertex_id_reuse(self):
        ir,family=fixture();ir['records']=[]
        with self.assertRaisesRegex(ValueError,'topology'):recover(ir,'missing',family)
    def test_bilateral_average_driver(self):
        d=dict(angleLow=0,angleHigh=-100,morph1Low=0,morph1High=1,clampMorphValue=True,_multiplier=1)
        for a,w in [(0,0),(30,.3),(60,.6),(90,.9),(100,1),(120,1),(-20,0)]:self.assertAlmostEqual(source_driver_weight(-a,-a,d),w)
        self.assertAlmostEqual(source_driver_weight(-90,0,d),.45)
