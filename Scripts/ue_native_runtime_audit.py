"""Read-only audit of explicit native Runtime bundles. VAM_NATIVE_RUNTIME_AUDIT
points to a JSON object with build_reports and report; no latest-file discovery.
Legacy BP loading is exercised separately before migration.
"""
import json, os
from pathlib import Path
import unreal as u

spec=json.loads(Path(os.environ['VAM_NATIVE_RUNTIME_AUDIT']).read_text(encoding='utf-8-sig'))
registry=u.AssetRegistryHelpers.get_asset_registry()
options=u.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True,
    include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
retired=('SoftTissue','FleshCapability','TissueBackend','ChaosFlesh','ProceduralMesh','Deformable')
rows=[]
for path in spec['build_reports']:
    build=json.loads(Path(path).read_text(encoding='utf8'))
    assert build['status']=='committed'
    bp=u.load_asset(build['blueprint']);assert bp
    cdo=u.get_default_object(bp.generated_class())
    config=cdo.get_editor_property('character').get_editor_property('runtime_configuration')
    assert config.get_editor_property('schema_version')==3
    assert config.get_editor_property('independent_reload_verified')
    receipt=json.loads(config.get_editor_property('receipt_json'))
    assert receipt['schema']=='vam-runtime-receipt/3' and 'soft_tissue' not in receipt['recipe']
    components=[c.get_class().get_name() for c in cdo.get_components_by_class(u.ActorComponent)]
    assert not any(word in name for name in components for word in retired), components
    pending=[build['blueprint'].split('.')[0],build['configuration'].split('.')[0]];seen=set()
    while pending:
        package=pending.pop()
        if package in seen:continue
        seen.add(package)
        assert not any(word in package for word in retired),package
        assert not package.startswith('/Game/VamRuntimeTests/'),package
        if package.startswith('/Script/'):continue
        for data in registry.get_assets_by_package_name(package):
            name=str(data.asset_class_path)
            assert not any(word in name for word in retired),(package,name)
        pending.extend(str(p) for p in registry.get_dependencies(package,options))
    rows.append(dict(blueprint=build['blueprint'],configuration=config.get_path_name(),schema=3,
        components=components,dependency_packages=sorted(seen)))
Path(spec['report']).write_text(json.dumps(dict(native_runtime_dependencies_passed=True,subjects=rows),indent=2),encoding='utf8')
