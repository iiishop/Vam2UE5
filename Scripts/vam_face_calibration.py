"""Geometry/camera driven face calibration, independent of person and world scale.

Requires neutral skin-only input and locally supplied official head landmarks.
Ambiguous rays fail closed; no back-of-head or guessed eyelid depth anchors.
"""
import math
import numpy as np

class CalibrationError(ValueError): pass

class Camera:
    def __init__(self, calibration):
        view=calibration['CameraViewInfo'];size=calibration['ImageSize']
        self.origin=np.array([view['Location'][k] for k in ('X','Y','Z')],float)
        pitch,yaw,roll=np.radians([view['Rotation'][k] for k in ('Pitch','Yaw','Roll')])
        forward=np.array([math.cos(pitch)*math.cos(yaw),math.cos(pitch)*math.sin(yaw),math.sin(pitch)])
        right=np.array([-math.sin(yaw),math.cos(yaw),0.]);up=np.cross(forward,right)
        self.basis=np.array([right*math.cos(roll)+up*math.sin(roll),
                             up*math.cos(roll)-right*math.sin(roll),forward]).T
        self.center=np.array([size['X']/2,size['Y']/2]);self.focal=size['X']/(2*math.tan(math.radians(view['FOV'])/2))
    def project(self, vertices):
        q=(np.asarray(vertices)-self.origin)@self.basis
        if np.any(q[:,2]<=0):raise CalibrationError('GeometryBehindCamera')
        return self.center+q[:,:2]*[1,-1]*self.focal/q[:,2,None],q[:,2]
    def ray(self,pixel):
        xy=(np.asarray(pixel)-self.center)/self.focal
        return self.basis@np.array([xy[0],-xy[1],1.])

def surface_hit(vertices, triangles, camera, pixel, reference_depth, depth_tolerance):
    v=np.asarray(vertices);t=np.asarray(triangles).reshape(-1,3)
    e1=v[t[:,1]]-v[t[:,0]];e2=v[t[:,2]]-v[t[:,0]];s=camera.origin-v[t[:,0]]
    ray=camera.ray(pixel);h=np.cross(ray,e2);det=np.sum(e1*h,axis=1)
    inv=np.divide(1,det,out=np.zeros_like(det),where=abs(det)>1e-12)
    a=inv*np.sum(s*h,axis=1);q=np.cross(s,e1);b=inv*(q@ray)
    distance=inv*np.sum(e2*q,axis=1)
    valid=(abs(det)>1e-12)&(a>=0)&(b>=0)&(a+b<=1)&(distance>0)
    if not valid.any():raise CalibrationError('SurfaceRayMiss')
    i=int(np.argmin(np.where(valid,distance,np.inf)))
    if abs(distance[i]-reference_depth)>depth_tolerance:raise CalibrationError('SurfaceRayDepthAmbiguous')
    return camera.origin+distance[i]*ray,t[i].tolist()

def generate(source_vertices, source_triangles, fitted_vertices, landmarks, calibration, include_surface_samples=False):
    """Return sparse anatomical profile constraints plus a conservative 2D set.

    No person name, asset path, fixed height, or fixed nose vertex ID is used.
    Eyelids stay 2D-only because a skin-only scan has open eye sockets.
    Nasolabial/philtrum shading tracks are excluded: they are not hard borders.
    """
    import copy
    camera=Camera(calibration);source=np.asarray(source_vertices);fit=np.asarray(fitted_vertices)
    sp,sd=camera.project(source);fp,fd=camera.project(fit)
    curves=calibration['CurveTrackingPoints']
    def pixels(name):return np.array([[p['X'],p['Y']] for p in curves[name]['TrackingPoints']])
    eyes=[n for n in curves if 'eyelid' in n];lips=[n for n in curves if 'lip_upper_outer' in n]
    if len(eyes)<4 or len(lips)<2:raise CalibrationError('MissingEyeOrLipContours')
    source_eyes=np.concatenate([pixels(n) for n in eyes]);source_lips=np.concatenate([pixels(n) for n in lips])
    eye_ids=np.unique([i for n in eyes for i in landmarks[n]['vIDs']]);lip_ids=np.unique([i for n in lips for i in landmarks[n]['vIDs']])
    def frame(e,l):
        width=float(np.ptp(e[:,0]));center=float((e[:,0].min()+e[:,0].max())/2)
        ey=float(np.median(e[:,1]));mouth=float(np.min(l[:,1]));gap=mouth-ey
        if width<=0 or not .15*width<gap<width:raise CalibrationError('InvalidFaceFrame')
        return center,ey,mouth,width
    sf=frame(source_eyes,source_lips);mf=frame(fp[eye_ids],fp[lip_ids])
    def tip(projected,depth,frame):
        x,eye,mouth,width=frame;gap=mouth-eye
        mask=(abs(projected[:,0]-x)<.12*width)&(projected[:,1]>eye+.15*gap)&(projected[:,1]<mouth-.15*gap)
        ids=np.flatnonzero(mask)
        if len(ids)<3:raise CalibrationError('NoseRegionTooSparse')
        relief=float(np.median(depth[ids])-np.min(depth[ids]))
        if relief < .01*width*float(np.median(depth[ids]))/camera.focal:
            raise CalibrationError('AmbiguousNoseProfile')
        return int(ids[np.argmin(depth[ids])])
    si=tip(sp,sd,sf);mi=tip(fp,fd,mf)
    # Scale of the observed face at the depth of the fitted eye contours.
    ref=float(np.median(fd[eye_ids]));tolerance=.45*mf[3]*ref/camera.focal
    points={str(mi):source[si].tolist()};provenance={str(mi):{'feature':'nose_tip','source_vertex':si}}
    def add(feature,sx,sy,mx,my):
        delta=fp-np.array([mx,my]);mask=(abs(delta[:,0])<.025*mf[3])&(abs(delta[:,1])<.025*mf[3])&(abs(fd-ref)<tolerance)
        ids=np.flatnonzero(mask)
        if len(ids)==0:raise CalibrationError('MissingModelProfileSample:'+feature)
        idx=int(ids[np.argmin(np.sum(delta[ids]**2,axis=1))])
        pos,tri=surface_hit(source,source_triangles,camera,[sx,sy],ref,tolerance)
        if str(idx) in points:return
        points[str(idx)]=pos.tolist();provenance[str(idx)]={'feature':feature,'pixel':[sx,sy],'source_triangle':tri}
    for fraction in (0.,.3,.6):
        add('nose_bridge_'+str(fraction),sf[0],sf[1]+fraction*(sp[si,1]-sf[1]),mf[0],mf[1]+fraction*(fp[mi,1]-mf[1]))
    for lateral in (-.085,0.,.085):
        add('nose_base_'+str(lateral),sf[0]+lateral*sf[3],sp[si,1]+.4*(sf[2]-sp[si,1]),mf[0]+lateral*mf[3],fp[mi,1]+.4*(mf[2]-fp[mi,1]))
    rejected=[]
    # Cover the surrounding skin, not just an isolated tip. Positions are
    # normalized by observed eye span and eye-to-lip distance in each mesh.
    # This constrains nose/cheek transitions without preserving a guessed fold.
    for y in (.12,.3,.5,.7,.9,1.1):
        if not include_surface_samples:break
        for x in (-.32,-.24,-.16,-.08,0.,.08,.16,.24,.32):
            if y<.25 and abs(x)>.15:continue  # open eye sockets
            if y>.82 and abs(x)<.22:continue  # mouth aperture / separate lip constraints
            feature='surface_%g_%g'%(x,y)
            try:
                add(feature,sf[0]+x*sf[3],sf[1]+y*(sf[2]-sf[1]),
                    mf[0]+x*mf[3],mf[1]+y*(mf[2]-mf[1]))
            except CalibrationError as exc:
                rejected.append({'feature':feature,'reason':str(exc)})
    # Lip border correspondence follows lateral position rather than assuming
    # the official vertex-ID list is in curve traversal order.
    for name in curves:
        if not ('lip_upper_outer' in name or 'lip_lower_outer' in name):continue
        ids=sorted(landmarks[name]['vIDs'],key=lambda i:fp[i,0]);p=pixels(name);p=p[np.argsort(p[:,0])]
        for fraction in (0.,.5,1.):
            x=fp[ids[0],0]+fraction*(fp[ids[-1],0]-fp[ids[0],0]);idx=min(ids,key=lambda i:abs(fp[i,0]-x))
            sx=p[0,0]+fraction*(p[-1,0]-p[0,0]);sy=float(np.interp(sx,p[:,0],p[:,1]))
            pos,tri=surface_hit(source,source_triangles,camera,[sx,sy],ref,tolerance)
            points[str(idx)]=pos.tolist();provenance[str(idx)]={'feature':name,'pixel':[float(sx),sy],'source_triangle':tri}
    filtered=copy.deepcopy(calibration)
    filtered['CurveTrackingPoints']={n:c for n,c in curves.items() if 'eyelid' in n or ('lip_' in n and ('outer' in n or 'inner' in n))}
    return {'schema':'vam-face-calibration/3','keypoints':points,'provenance':provenance,'calibration':filtered,'rejected_surface_samples':rejected,
            'experimental_surface_samples':include_surface_samples,'auto_promote':False,
            'eye_constraints':'2D only; no guessed socket depth','visual_acceptance_passed':False}
