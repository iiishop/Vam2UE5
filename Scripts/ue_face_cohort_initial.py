"""Cohort initial fit using identical local official stages for every preset.

No stored Qimeng anchors or character-specific calibration are imported.
All tracker evidence and intermediate geometry remain auditable and provisional.
"""
import json,os,sys
from pathlib import Path
import unreal as u
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'Saved/Python'));sys.path.insert(0,str(ROOT/'Scripts'))
import numpy as np
from PIL import Image
from vam_metahuman import write_json,verify_recipe
from vam_native_job_state import exclusive_build
from ue_metahuman_job import run,owned,checkpoint,reload_check


def export_stage(character,target,out):
    out.mkdir(exist_ok=True)
    sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
    posed=json.loads(u.VamMetaHumanEditorAdapter.inspect_fit_geometry(character,target,True))
    apose=json.loads(u.VamMetaHumanEditorAdapter.inspect_fit_geometry(character,target,False))
    write_json(out/'body-posed.json',posed);write_json(out/'body-apose.json',apose)
    a=np.asarray(apose['vertices'])[:,[0,2,1]];b=np.asarray(posed['vertices'])[:,[0,2,1]]
    mask=b[:,2]>b[:,2].max()-.04*np.ptp(b[:,2]);ac=a[mask].mean(0);bc=b[mask].mean(0)
    uu,_,vv=np.linalg.svd((a[mask]-ac).T@(b[mask]-bc));rotation=uu@vv
    error=float(np.max(np.linalg.norm((a[mask]-ac)@rotation+bc-b[mask],axis=1)))
    if np.linalg.det(rotation)<0 or error>.05:raise ValueError('NonRigidOfficialHeadPose')
    actor=sub.spawn_meta_human_actor(character,True)
    try:
        c=next(c for c in actor.get_components_by_class(u.SkeletalMeshComponent) if c.get_name()=='Face')
        mesh=c.get_editor_property('skeletal_mesh_asset');vs,ts=sub.get_mesh_data_for_conforming(mesh)
        full=np.array([[v.x,v.y,v.z] for v in vs]);sections=json.loads(u.VamMetaHumanEditorAdapter.inspect_mesh_sections(mesh))
        skin=next(s for s in sections['sections'] if s['slot']=='head_shader_shader');count=max(skin['triangles'])+1
        if set(skin['triangles'])!=set(range(count)):raise ValueError('OfficialHeadIdentityAmbiguous')
        head={'vertices':((full[:count]-ac)@rotation+bc).tolist(),'apose_vertices':full[:count].tolist(),
            'triangles':skin['triangles'],'initial_fit_provenance':{'api':'ConformToTargetMeshes','character':character.get_path_name(),
            'pose_alignment':'MH body A-pose to MH saved target pose; no alignment to source face','max_rigid_error_cm':error}}
        write_json(out/'head.json',head);write_json(out/'actual-face.json',{'vertices':full.tolist(),'triangles':list(ts)})
        write_json(out/'sections.json',sections)
        return head
    finally:u.get_editor_subsystem(u.EditorActorSubsystem).destroy_actor(actor)


def main():
    r=json.loads(Path(os.environ['VAM_COHORT_REQUEST']).read_text(encoding='utf8'));out=Path(r['output']);job=out/'Initial'
    os.environ['VAM_BUILD_JOB']=str(job)
    with exclusive_build(ROOT/'Saved'):
        run(job,'fit')
        recipe=verify_recipe(job);character=owned(recipe,'character');target=owned(recipe,'target')
        sub=u.get_editor_subsystem(u.MetaHumanCharacterEditorSubsystem)
        if not sub.try_add_object_to_edit(character):raise ValueError('CharacterRegistrationFailed')
        try:
            if not (out/'Coarse/head.json').exists():export_stage(character,target,out/'Coarse')
            if not recipe.get('cohort_tracked_fit_complete'):
                image=Image.open(out/'tracking-input.png').convert('RGB')
                curves=sub.track_face_landmarks_from_image([u.Color(rr,g,b,255) for rr,g,b in image.getdata()],image.width,image.height)
                if not curves:raise ValueError('LocalFaceTrackerFailed')
                calibration=json.loads((out/'tracking-camera.json').read_text(encoding='utf8'))
                calibration.pop('framing_evidence',None)
                calibration['CurveTrackingPoints']={str(k):{'TrackingPoints':[{'X':p.x,'Y':p.y} for p in v.tracking_points]} for k,v in curves.items()}
                write_json(out/'tracker-calibration.json',calibration)
                # Keep the official combined solve as a separate measured stage.
                # Tracker evidence does not become confirmed source semantics.
                error=u.VamMetaHumanEditorAdapter.conform(character,target,{},json.dumps(calibration))
                if error is None or error:raise ValueError('TrackedConformFailed:'+str(error))
                recipe.update(calibration=calibration,cohort_tracked_fit_complete=True,fit_origin='CohortTrackedOfficialConform')
                checkpoint(job,recipe,character,'Draft')
            export_stage(character,target,out/'Tracked')
        finally:sub.remove_object_to_edit(character)
        reload_check(job,'source')
        write_json(out/'initial-status.json',{'state':'VerifiedInitialDraft','character':recipe['assets']['character'],
            'source_semantics_verified':False,'visual_acceptance_passed':False,'settings':'same default combined official solve and source-bounds tracker camera for every cohort member'})


if __name__=='__main__':main()
