"""Explicit diagnostic official baseline; never accepts a failed residual solve.

Runs the existing scoped Epic lifecycle and independent exports for a cohort
member whose raw official fit is intentionally being studied with its defects.
"""
import argparse,json,os,subprocess,sys
from pathlib import Path
from vam_metahuman import write_json,fingerprint


def read(p):return json.loads(Path(p).read_text(encoding='utf8'))


def run(request):
    r=read(request);root=Path(r['output']);job=root/'Initial';scripts=Path(__file__).resolve().parent
    status=read(root/'initial-status.json')
    if status['state']!='VerifiedInitialDraft':raise ValueError('InitialDraftNotVerified')
    recipe=read(job/'recipe.json');character=recipe['assets']['character']
    exe=Path(r['engine'])/'Engine/Binaries/Win64/UnrealEditor-Cmd.exe'
    def ue(script,label,variables=None,args=()):
        env=os.environ.copy();env.update(variables or {})
        cmd=[str(exe),r['project'],'-run=pythonscript','-script='+str(scripts/script),'-AllowCommandletRendering',
            '-ini:Engine:[ConsoleVariables]:TextureGraph.AllowCommandlets=1','-unattended','-nosplash','-abslog='+str(root/(label+'.log')),*args]
        print(json.dumps({'preset':r['preset'],'phase':label}),flush=True)
        with (root/(label+'-console.log')).open('w',encoding='utf8') as stream:
            result=subprocess.run(cmd,env=env,stdout=stream,stderr=subprocess.STDOUT,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        if result.returncode:raise ValueError('OfficialBaselinePhaseFailed:'+label)
    contract=read(job/'request.json')
    if contract.get('purpose')!='cross-preset-structural-baseline' or contract.get('diagnostic_only') is not True:
        raise ValueError('ExplicitDiagnosticRequestRequired')
    ue('ue_metahuman_job.py','baseline-lifecycle',args=('-VamMHJob='+str(job),'-VamMHMode=diagnostic-baseline'))
    recipe=read(job/'recipe.json');verified=read(job/'assembly-verified.json')
    if recipe['state']!='VerifiedEditorAssembly' or not recipe.get('diagnostic_only') or not verified['assembly']['valid']:
        raise ValueError('BaselineAssemblyVerificationFailed')
    for name in ('BaselineExport','BaselineReload'):
        export=root/name;req={'character':character,'output':str(export)}
        if name=='BaselineReload':
            req['expected']=str(root/'BaselineExport/actual-face.json')
            req['blueprint']=recipe['blueprint']
        path=root/(name+'-request.json');write_json(path,req)
        ue('ue_face_fidelity_export.py',name,{'VAM_FACE_EXPORT_REQUEST':str(path)})
    reload=read(root/'BaselineReload/reload.json')
    if not reload['full_rig'] or reload['max_reload_delta_cm']!=0:raise ValueError('BaselineRigReloadFailed')
    assembly_geometry=read(root/'BaselineReload/assembly-head-check.json')
    write_json(root/'baseline-lifecycle.json',{'state':'VerifiedDiagnosticEditorAssembly','quality_status':'DraftStructuralBaseline',
        'character':character,'bp':recipe['blueprint'],'assembly_valid':True,'full_rig':True,
        'max_reload_delta_cm':reload['max_reload_delta_cm'],'assembly_receipt_sha256':fingerprint(job/'assembly-verified.json'),
        'assembly_geometry':assembly_geometry,'residual_pipeline_accepted':False,'visual_acceptance_passed':False,
        'scope':'Full official lifecycle baseline, not a repaired residual result; no failed fidelity gate was accepted'})


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--request',required=True);a=p.parse_args();run(a.request)
