"""Source / previous / candidate skin-only eye crops in identical cameras."""
import argparse,json
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
from vam_face_fidelity import view_matrix
from vam_face_fidelity_views import render


def compare(job,previous,output):
    job=Path(job);previous=Path(previous)
    read=lambda p:json.loads(Path(p).read_text(encoding='utf8'))
    source=read(job/'source.json');head=read(job/'head.json')
    lm=read(read(job/'request.json')['landmarks'])
    def actual(root):
        export=root/'Official/RigReload/actual-head.json'
        if not export.exists():export=root/'Official/TemplateReload/actual-head.json'
        if not export.exists():raise ValueError('OfficialEyeViewRequiresActualExport')
        mesh=read(export);pose=read(root/'pose-transform.json');r=np.asarray(pose['posed_to_apose_rotation'])
        vertices=(np.asarray(mesh['vertices'])-pose['apose_center'])@r.T+pose['posed_center']
        return vertices,np.asarray(mesh['triangles']).reshape(-1,3),str(export)
    old,ot,op=actual(previous);new,nt,npth=actual(job)
    sv=np.asarray(source['vertices']);st=np.asarray(source['triangles']).reshape(-1,3)
    size=512;sheet=Image.new('RGB',(size*3,size*6));cameras=[]
    row=0
    for side in ('l','r'):
        ids=lm['crv_eyelid_upper_'+side]['vIDs']+lm['crv_eyelid_lower_'+side]['vIDs']
        points=np.asarray(head['vertices'])[ids];center=points.mean(0)
        radius=np.linalg.norm(points-center,axis=1).max()*1.45
        for yaw in (0,-45,45):
            cameras.append({'side':side,'yaw':yaw,'center':center.tolist(),'radius':float(radius)})
            for col,(v,t,title) in enumerate(((sv,st,'SOURCE p0'),(old,ot,'PREVIOUS skin'),(new,nt,'NEW official skin'))):
                tile=render(v,t,view_matrix(yaw,0),center,radius,size)
                ImageDraw.Draw(tile).text((8,8),f'{title} {side} yaw={yaw}',fill='white')
                sheet.paste(tile,(col*size,row*size))
            row+=1
    sheet.save(output)
    Path(output).with_suffix('.json').write_text(json.dumps({'cameras':cameras,'previous':op,'candidate':npth,
        'scope':'skin only; auxiliary eye sections and final materials not rendered','visual_pass':False},indent=2),encoding='utf8')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--job',required=True);p.add_argument('--previous',required=True);p.add_argument('--output',required=True)
    a=p.parse_args();compare(a.job,a.previous,a.output)
