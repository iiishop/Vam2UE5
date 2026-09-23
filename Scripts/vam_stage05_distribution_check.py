"""Audit actual UAT output, including original Content and runtime contracts."""
import argparse, hashlib, json
from pathlib import Path

def audit(project):
    source=project/'Plugins/VamResourceBrowser'
    package=project/'Saved/VamBrowserBuild'
    required=['VamResourceBrowser.uplugin','Build.ps1','Config/FilterPlugin.ini',
              'Config/Stage05MorphSet.json','Config/Stage05Quality.json',
              'Source/VamCharacterRuntime/VamCharacterRuntime.Build.cs',
              'Content/Blueprints/BP_VamCharacter.uasset',
              'Content/Examples/Stage05/SK_TestPanel.uasset',
              'Content/Examples/Stage05/BP_TestPanel.uasset',
              'Content/Examples/Stage05/CD_TestPanel.uasset',
              'Content/Examples/Stage05/L_ShapeValidation.umap',
              'Binaries/Win64/UnrealEditor-VamCharacterRuntime.dll',
              'Binaries/Win64/UnrealEditor-VamResourceBrowser.dll']
    for prefix in ('Source','Config','Content','Web'):
        required += [p.relative_to(source).as_posix() for p in (source/prefix).rglob('*') if p.is_file()]
    required += [p.relative_to(source).as_posix() for p in (source/'Scripts').glob('*.py')]
    required += ['STAGE02.md','STAGE03.md','STAGE04.md','STAGE05.md']
    hashes={}
    for name in sorted(set(required)):
        target=package/name
        assert target.is_file(),name
        hashes[name]=hashlib.sha256(target.read_bytes()).hexdigest()
        if not name.startswith('Binaries/') and name!='VamResourceBrowser.uplugin':
            assert target.read_bytes()==(source/name).read_bytes(),'stale package: '+name
    for p in package.rglob('*'):
        rel=p.relative_to(package)
        assert not {'Saved','__pycache__','AddonPackages','HostProject'}.intersection(rel.parts),str(rel)
        assert p.suffix.lower() not in ('.var','.lock','.pyc'),str(rel)
    descriptor=json.loads((package/'VamResourceBrowser.uplugin').read_text(encoding='utf8'))
    assert descriptor['CanContainContent']
    assert descriptor['EnabledByDefault'] is False
    original=json.loads((source/'VamResourceBrowser.uplugin').read_text(encoding='utf8'))
    for key in ('Modules','Plugins','SupportedTargetPlatforms','Version'):
        assert descriptor[key]==original[key],key
    assert all(p.get('TargetAllowList')==['Editor'] for p in descriptor['Plugins'])
    assert any(m['Name']=='VamCharacterRuntime' and m['Type']=='Runtime' for m in descriptor['Modules'])
    result={'status':'passed','package':str(package),'verified_files':len(hashes),'sha256':hashes,
            'excluded':'Saved, VAR, locks, cache, HostProject','source_content_matches':True}
    output=source/'Saved/NativeBuild/shape-distribution-check.json'
    output.write_text(json.dumps(result,indent=2),encoding='utf8')
    print(output)

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--project',type=Path,required=True)
    audit(parser.parse_args().project)
