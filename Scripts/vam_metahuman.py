"""MH00 local input adapter. No UE, cloud, Unity decoder or native runtime build."""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import zipfile

from vam_decode import finite, to_ue
from vam_fit import apply_morph
from vam_plan import canonical, sha, strict_json
from vam_zip_compat import find_member

SCHEMA = 'vam-metahuman-recipe/1'
TARGET_FRAME = 'creator-centimeters-x-lateral-y-forward-z-up/v2'


def to_creator(vertex):
    """Rotate native UE +X forward to Creator mesh +Y forward (yaw +90).

    Creator's mesh-import auto framing looks from +Y; CoreTech converts UE
    (X,Y,Z) into DNA (X,Z,Y). Do not use the native gameplay mesh frame here.
    This is a proper rotation and preserves the source polygon winding.
    """
    x, y, z = to_ue(vertex)
    return [-y, x, z]
SKIN = {'Legs', 'Nostrils', 'Lips', 'Face', 'Toenails', 'Fingernails', 'Head',
        'Hands', 'Shoulders', 'Neck', 'Hips', 'Torso', 'Forearms', 'Feet', 'Ears'}


def fingerprint(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def diagnostic_baseline_allowed(recipe, request, mode):
    """Separate official baseline experiments from production fidelity gates."""
    diagnostic = mode == 'diagnostic-baseline'
    if recipe.get('diagnostic_only') and not diagnostic:
        raise ValueError('DiagnosticBaselineRequiresExplicitEntry: not a production fidelity result')
    if diagnostic and (request.get('purpose') != 'cross-preset-structural-baseline'
            or request.get('diagnostic_only') is not True or not recipe.get('cohort_tracked_fit_complete')
            or recipe.get('fit_origin') != 'CohortTrackedOfficialConform'):
        raise ValueError('DiagnosticBaselineContractMissing')
    return diagnostic


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix('.tmp')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False), encoding='utf8')
    temporary.replace(path)


def user_name(name):
    # Reject rather than sanitize. Unicode names remain unchanged.
    if not name or name != name.strip() or any(not (c.isalnum() or c == '_') for c in name):
        raise ValueError('RenameRequired: use a nonempty user name containing letters, digits or underscore')
    return name


def probe(engine):
    engine = Path(engine)
    if (engine/'Engine').is_dir(): engine /= 'Engine'
    version = strict_json((engine/'Build/Build.version').read_bytes())
    plugins = {}
    for path in (engine/'Plugins').rglob('*.uplugin'):
        if path.stem in {'MetaHumanCharacter', 'RigLogic', 'HairStrands', 'ChaosClothAsset',
                         'ChaosClothAssetEditor', 'ChaosOutfitAsset', 'MetaHumanCoreTech'}:
            data = strict_json(path.read_bytes())
            plugins[path.stem] = {'path': str(path), 'sha256': fingerprint(path),
                'modules': data.get('Modules', []), 'dependencies': data.get('Plugins', [])}
    mh = engine/'Plugins/MetaHuman/MetaHumanCharacter'
    optional = mh/'Content/Optional'
    core = {'texture_models': len(list((optional/'TextureSynthesis').rglob('*.ar'))),
            'body_textures': len(list((optional/'BodyTextures').glob('*.uasset')))}
    header = mh/'Source/MetaHumanCharacterEditor/Public/MetaHumanCharacterEditorSubsystem.h'
    source = header.read_text(encoding='utf8') if header.exists() else ''
    return {'schema': 'vam-metahuman-capabilities/1', 'engine': version, 'plugins': plugins,
        'core_files': core, 'core_data': all(core.values()),
        'header': str(header), 'header_sha256': fingerprint(header) if header.exists() else None,
        'public_symbols': {name: bool(re.search(r'\b'+name+r'\s*\(', source)) for name in
            ['ConformToTargetMeshes', 'ConformTargetMeshesAsync', 'OnAsyncMeshConformCompleted',
             'CancelMeshAsyncProcess', 'RequestAutoRigging', 'BuildMetaHuman', 'ImportFromTemplate']},
        'scope': 'Local file probe only; enabled modules, rendering, authentication and Cook require UE checks',
        'cloud_authorized': False, 'visual_acceptance_passed': False}


def verify_plan(plan, ir):
    identity = plan['plan_id']
    payload = dict(plan); del payload['plan_id']
    if sha(canonical(payload)) != identity or ir['plan_id'] != identity:
        raise ValueError('SourceLockChanged: invalid plan identity')
    root = Path(plan['source_root']).resolve()
    def source_path(relative):
        result = (root/relative).resolve()
        if not result.is_relative_to(root) or not result.is_file():
            raise ValueError('SourceUnavailable: '+relative)
        return result
    for relative, expected in ir['source_hashes'].items():
        if fingerprint(source_path(relative)) != expected:
            raise ValueError('SourceLockChanged: '+relative)
    # Verify locked file/VAR members, without decoding or executing any source scripts.
    for item in plan['items']:
        expected = item.get('sha256')
        if not expected or item.get('builtin_mapping'): continue
        if item.get('source'):
            with zipfile.ZipFile(source_path(item['source'])) as archive:
                with archive.open(find_member(archive, item['path'])) as stream:
                    actual = hashlib.file_digest(stream, 'sha256').hexdigest()
        else: actual = fingerprint(source_path(item['path']))
        if actual != expected: raise ValueError('SourceLockChanged: '+item['path'])


def neutral_target(ir, overrides=None):
    """Evaluate p0 deltas on original ungrafted body. Never skin or apply pose/physics."""
    overrides = overrides or {}
    merged = [r for r in ir['records'] if r.get('class') == 'DAZMergedMesh']
    if len(merged) != 1: raise ValueError('SourceBodyAmbiguous')
    target_id = str(merged[0]['parameters']['targetMesh']['m_PathID'])
    target = next(r for r in ir['records'] if r.get('kind') == 'unity_mesh' and r['object'] == target_id)
    mesh = copy.deepcopy(target['mesh'])
    morphs = {r['id']: r for r in ir['records'] if r.get('kind') == 'morph'}
    included, excluded, formulas = [], [], []
    for application in ir['applied_morphs']:
        record = morphs[application['id']]; params = record['data']['parameters']
        tags = ' '.join(str(params.get(k, '')) for k in ('group', 'region')).casefold()
        pose = str(params.get('isPoseControl', '')).casefold() in ('true', '1')
        # Some builtin controllers explicitly carry isPoseControl=false while
        # their authored group/region is Pose Controls/Hands/... . The category
        # is still direct pose metadata; do not bake those into identity.
        pose_category = any(part.strip().casefold() == 'pose controls'
            for key in ('group', 'region') for part in str(params.get(key, '')).replace('\\', '/').split('/'))
        expression = 'expression' in tags or pose or pose_category
        reason = 'graft' if application.get('vertex_offset', 0) else ('expression_or_pose' if expression else '')
        choice = overrides.get(application['id'])
        if choice not in (None, 'include', 'exclude'): raise ValueError('InvalidMorphClassification')
        if choice == 'exclude': reason = 'recipe_exclusion'
        if choice == 'include' and reason != 'graft': reason = ''
        entry = {'id': application['id'], 'name': params.get('displayName', record['path']), 'value': application['value']}
        if reason: excluded.append(dict(entry, reason=reason)); continue
        apply_morph(mesh, record['data'], application['value'], len(mesh['vertices']), len(mesh['uv']))
        included.append(entry)
        if params.get('formulas'): formulas.append({'id': application['id'], 'formulas': params['formulas']})
    selected = [p for p in mesh['polygons'] if mesh['materials'][p['materialNum']] in SKIN]
    used = sorted({i for p in selected for i in p['vertices']})
    remap = {v: i for i, v in enumerate(used)}
    triangles = []
    triangle_materials = []
    for polygon in selected:
        indices = [remap[v] for v in polygon['vertices']]
        # Source polygons already match the native skeletal mesh / Creator
        # MeshDescription winding. Both axis conversions here have determinant
        # +1. Reversing corners turns the solver surface inside-out.
        for i in range(1, len(indices)-1):
            triangles.extend((indices[0], indices[i], indices[i+1]))
            triangle_materials.append(mesh['materials'][polygon['materialNum']])
    result = {'schema': 'vam-metahuman-target/2', 'vertices': [to_creator(mesh['vertices'][i]) for i in used],
        'triangles': triangles, 'input_to_source_vertex': used, 'source_object': target_id,
        'triangle_materials': triangle_materials,
        'source_polygons': [[remap[v] for v in p['vertices']] for p in selected],
        'source_topology_family': {'Genesis2Female': 'G2F', 'Genesis2Male': 'G2M'}.get(mesh.get('names', {}).get('sceneNodeId'), 'Unknown'),
        'source_base_topology_sha256': sha(canonical({'vertex_count': len(mesh['vertices']),
            'polygons': [p['vertices'] for p in mesh['polygons']]})),
        'included_morphs': included, 'excluded_morphs': excluded,
        'included_materials': sorted(SKIN.intersection(mesh['materials'])),
        'excluded_materials': sorted(set(mesh['materials'])-SKIN),
        'formula_provenance': formulas, 'coordinate_frame': TARGET_FRAME,
        'coordinates': 'Creator mesh centimeters: (-source.x, source.z, source.y)*100',
        'native_to_creator': [[0,-1,0],[1,0,0],[0,0,1]],
        'limitations': ['Neutral means tagged expression/pose morphs are zero; combined sculpted expressions require recipe classification.',
            'Target is p0 vertex geometry; source rig scale/orientation/rotation formulas are retained, not evaluated.',
            'Graft, nipple material, mouth interior, eyes, lashes, hair, clothing and accessories are excluded; no closure is synthesized.']}
    finite(result)
    if not used or not triangles: raise ValueError('EmptyTarget')
    return result


def prepare(data, preview_path, name, destination, job, engine, overrides=None, keypoints=None, calibration=None):
    user_name(name)
    if not re.fullmatch(r'/Game(?:/[\w]+)+', destination, flags=re.UNICODE):
        raise ValueError('InvalidDestination: use an explicit /Game folder')
    data, job = Path(data), Path(job)
    if (job/'recipe.json').exists(): raise ValueError('RecipeExists: resume instead of overwriting')
    preview_path = Path(preview_path).resolve()
    preview = strict_json(preview_path.read_bytes())
    decode_id = preview['decode_id']
    if not re.fullmatch('[a-f0-9]{64}', decode_id): raise ValueError('InvalidDecodeIdentity')
    ir_path = data/'Decoded'/(decode_id+'.ir.json')
    ir = strict_json(ir_path.read_bytes())
    if ir['decode_id'] != decode_id or preview['plan_id'] != ir['plan_id']: raise ValueError('SourceIdentityMismatch')
    plan_path = data/'Plans'/(ir['plan_id']+'.json')
    plan = strict_json(plan_path.read_bytes()); verify_plan(plan, ir)
    material_path = Path(preview['source_material_ir']).resolve()
    material = strict_json(material_path.read_bytes())
    target = neutral_target(ir, overrides)
    job.mkdir(parents=True, exist_ok=True)
    write_json(job/'target.json', target)
    caps = probe(engine); write_json(job/'capabilities.json', caps)
    # Raw source/material payload stays in the local recipe directory; never enters a cloud request.
    inputs = {}
    for key, path in [('preview', preview_path), ('source_ir', ir_path), ('material_ir', material_path), ('plan', plan_path)]:
        inputs[key] = {'path': str(path.resolve()), 'sha256': fingerprint(path)}
    recipe = {'schema': SCHEMA, 'revision': 1, 'name': name, 'destination': destination,
        'decode_id': decode_id, 'plan_id': ir['plan_id'], 'inputs': inputs,
        'target_sha256': fingerprint(job/'target.json'), 'target_frame': TARGET_FRAME, 'engine': caps['engine'],
        'morph_classification': overrides or {}, 'keypoints': keypoints or {},
        'calibration': calibration or {},
        'assets': {'character': destination+'/'+name+'/Source/'+name,
                   'target': destination+'/'+name+'/Source/SM_ConformTarget',
                   'mapping': destination+'/'+name+'/Source/DA_SourceMapping',
                   'assembly': destination+'/'+name+'/Assembly'},
        'state': 'Draft', 'cloud_authorized': False, 'visual_acceptance_passed': False}
    finite(recipe)
    write_json(job/'recipe.json', recipe)
    return recipe


def verify_recipe(job):
    job = Path(job); recipe = strict_json((job/'recipe.json').read_bytes())
    if recipe['schema'] != SCHEMA: raise ValueError('RecipeVersionUnsupported')
    user_name(recipe['name'])
    if 'assets' in recipe:
        root = recipe['destination']+'/'+recipe['name']+'/'
        for path in recipe['assets'].values():
            if not path.startswith(root) or not re.fullmatch(r'/Game(?:/[\w]+)+', path, flags=re.UNICODE):
                raise ValueError('RecipeAssetPathOutsideCharacter')
    for entry in recipe['inputs'].values():
        if fingerprint(entry['path']) != entry['sha256']: raise ValueError('RecipeInputChanged: '+entry['path'])
    if fingerprint(job/'target.json') != recipe['target_sha256']: raise ValueError('TargetChanged')
    finite(recipe)
    return recipe


def require_current_target_frame(job, recipe):
    """Old task hashes remain auditable, but cannot authorize more bad output."""
    target = strict_json((Path(job)/'target.json').read_bytes())
    if (recipe.get('target_frame') != TARGET_FRAME or
            target.get('coordinate_frame') != TARGET_FRAME or
            target.get('schema') != 'vam-metahuman-target/2'):
        raise ValueError('LegacyTargetFrame: old native-axis/reversed-winding input is not publishable. Preserve existing assets and create a new named/versioned task from the locked input.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--engine', required=True); parser.add_argument('--out', required=True)
    parser.add_argument('--data'); parser.add_argument('--preview'); parser.add_argument('--name')
    parser.add_argument('--destination', default='/Game/MetaHumans')
    args = parser.parse_args()
    if args.preview:
        value = prepare(args.data, args.preview, args.name, args.destination, args.out, args.engine)
    else:
        value = probe(args.engine); Path(args.out).parent.mkdir(parents=True, exist_ok=True); write_json(args.out, value)
    print(json.dumps({'state': value.get('state'), 'core_data': value.get('core_data'), 'output': args.out}))
