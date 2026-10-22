"""Algebraic invariants of direction-only corrective transport (not UE proof)."""
import unittest
import numpy as np

def transported(m,ref,base,knee,other):
    return (np.r_[base+knee+other,1.]@m+np.r_[knee,0.]@(ref-m))[:3]

class TransportTests(unittest.TestCase):
    def test_keeps_m_base_and_other_morphs(self):
        m=np.diag([1.2,.8,1.,1.]);m[3,:3]=[3,4,5];ref=np.eye(4)
        p=np.array([1.,2.,3.]);k=np.array([.2,-.3,.4]);o=np.array([.6,.2,-.1])
        expected=(np.r_[p+o,1.]@m+np.r_[k,0.]@ref)[:3]
        np.testing.assert_allclose(transported(m,ref,p,k,o),expected)
    def test_no_knee_delta_no_change(self):
        m=np.diag([1.4,.7,1.,1.]);p=np.array([1.,2.,3.])
        np.testing.assert_allclose(transported(m,np.eye(4),p,np.zeros(3),np.zeros(3)),(np.r_[p,1.]@m)[:3])
    def test_jiggle_translation_survives(self):
        m=np.diag([1.4,.7,1.,1.]);ref=np.eye(4);p=np.ones(3);k=np.array([.2,.3,.4]);zero=np.zeros(3)
        before=transported(m,ref,p,k,zero);m[3,:3]+=[.4,-.5,.6]
        np.testing.assert_allclose(transported(m,ref,p,k,zero)-before,[.4,-.5,.6])
    def test_blended_operator_distributes(self):
        a=np.diag([1.2,.8,1.,1.]);b=np.diag([.9,1.1,1.,1.]);p=np.ones(3);k=np.array([.2,.3,.4]);z=np.zeros(3);r=np.eye(4)
        np.testing.assert_allclose(transported(.3*a+.7*b,r,p,k,z),.3*transported(a,r,p,k,z)+.7*transported(b,r,p,k,z))
if __name__=='__main__':unittest.main(verbosity=2)
