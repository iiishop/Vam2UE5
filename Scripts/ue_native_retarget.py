"""Deterministic Quinn -> VaM retarget assets built from verified source bone IDs.

The generated IK rigs never use UE's Daz auto-characterizer. Unknown hierarchies
fail rather than silently publishing zero-length chains or sideways animations.
The source mesh is optional for projects without the Third Person template; a
validated target IK Rig is still emitted for every supported VaM skeleton.
"""
import json
import math
from pathlib import Path

import unreal as u

PLUGIN = Path(__file__).resolve().parents[1]
CONFIG = PLUGIN / 'Config' / 'RetargetSource.json'
CHAINS = (
    ('Spine', 'spine_01', 'spine_05', 'abdomen', 'chest', 8.0),
    ('LeftArm', 'clavicle_l', 'hand_l', 'lCollar', 'lHand', 20.0),
    ('RightArm', 'clavicle_r', 'hand_r', 'rCollar', 'rHand', 20.0),
    ('LeftLeg', 'thigh_l', 'ball_l', 'lThigh', 'lToe', 35.0),
    ('RightLeg', 'thigh_r', 'ball_r', 'rThigh', 'rToe', 35.0),
    ('Head', 'neck_01', 'head', 'neck', 'head', 4.0),
)
CLIP_NAMES = {'idle': 'A_VamIdle', 'walk_forward': 'A_VamWalkForward', 'jog_forward': 'A_VamJogForward'}


def _require(condition, message):
    if not condition:
        raise ValueError('VaM retarget: ' + message)


def _xyz(transform):
    value = transform.translation
    return (float(value.x), float(value.y), float(value.z))


def _distance(a, b):
    return math.dist(_xyz(a), _xyz(b))


def _contract_check(bones):
    """Source identity, ancestry and physical span must all agree."""
    index = {bone['name']: i for i, bone in enumerate(bones)}
    _require(len(index) == len(bones), 'duplicate source bone names')
    _require('hip' in index and bones[index['hip']]['parent'] == -1, 'hip must be the source root')
    for chain, _, _, start, end, minimum in CHAINS:
        _require(start in index and end in index, f'{chain}: missing {start} or {end}')
        cursor = index[end]
        seen = set()
        while cursor >= 0 and cursor != index[start]:
            _require(cursor not in seen, f'{chain}: cyclic hierarchy')
            seen.add(cursor)
            cursor = bones[cursor]['parent']
        _require(cursor == index[start], f'{chain}: {start} is not an ancestor of {end}')
        a, b = bones[index[start]]['world_bind'], bones[index[end]]['world_bind']
        span = math.dist([float(a[i][3]) for i in range(3)], [float(b[i][3]) for i in range(3)])
        _require(math.isfinite(span) and span >= minimum, f'{chain}: collapsed source chain ({span:.2f} cm)')
    head = bones[index['head']]['world_bind'][2][3]
    foot = bones[index['lFoot']]['world_bind'][2][3]
    _require(head - foot > 80, 'source bind is not upright')


def _rig(name, folder, mesh, root, source):
    path = folder + '/' + name
    rig = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else \
        u.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, u.IKRigDefinition, u.IKRigDefinitionFactory())
    _require(isinstance(rig, u.IKRigDefinition), f'{path} is not an IK Rig')
    controller = u.IKRigController.get_controller(rig)
    if controller.get_skeletal_mesh() is None:
        _require(controller.set_skeletal_mesh(mesh), f'{path}: preview mesh rejected')
        _require(controller.set_retarget_root(root), f'{path}: invalid retarget root {root}')
        for chain, src_start, src_end, dst_start, dst_end, minimum in CHAINS:
            start, end = (src_start, src_end) if source else (dst_start, dst_end)
            _require(str(controller.add_retarget_chain(chain, start, end, 'None')) == chain,
                     f'{path}: could not create {chain}')
    _require(controller.get_skeletal_mesh() == mesh and str(controller.get_retarget_root()) == root,
             f'{path}: existing IK Rig is incompatible; user changes are protected')
    metrics = {}
    for chain, src_start, src_end, dst_start, dst_end, minimum in CHAINS:
        start, end = (src_start, src_end) if source else (dst_start, dst_end)
        _require(str(controller.get_retarget_chain_start_bone(chain)) == start and
                 str(controller.get_retarget_chain_end_bone(chain)) == end,
                 f'{path}: {chain} maps to different bones')
        span = _distance(controller.get_ref_pose_transform_of_bone(start),
                         controller.get_ref_pose_transform_of_bone(end))
        _require(math.isfinite(span) and span >= minimum, f'{path}: {chain} is {span:.2f} cm')
        metrics[chain] = round(span, 3)
    return rig, metrics


def _retargeter(folder, source_rig, target_rig, source_mesh, target_mesh):
    path = folder + '/RTG_QuinnToVam'
    asset = u.load_asset(path) if u.EditorAssetLibrary.does_asset_exist(path) else \
        u.AssetToolsHelpers.get_asset_tools().create_asset('RTG_QuinnToVam', folder, u.IKRetargeter, u.IKRetargetFactory())
    _require(isinstance(asset, u.IKRetargeter), f'{path} has the wrong class')
    controller = u.IKRetargeterController.get_controller(asset)
    source, target = u.RetargetSourceOrTarget.SOURCE, u.RetargetSourceOrTarget.TARGET
    if controller.get_ik_rig(source) is None and controller.get_ik_rig(target) is None:
        controller.set_ik_rig(source, source_rig)
        controller.set_ik_rig(target, target_rig)
        controller.set_preview_mesh(source, source_mesh)
        controller.set_preview_mesh(target, target_mesh)
        controller.add_default_ops()
        for chain, *_ in CHAINS:
            _require(controller.set_source_chain(chain, chain), f'{path}: {chain} was not mapped')
    _require(controller.get_ik_rig(source) == source_rig and controller.get_ik_rig(target) == target_rig,
             f'{path}: existing source/target IK Rig changed')
    _require(controller.get_preview_mesh(source) == source_mesh and controller.get_preview_mesh(target) == target_mesh,
             f'{path}: existing preview mesh changed')
    _require(controller.get_num_retarget_ops() >= 2, f'{path}: retarget operation stack empty')
    for chain, *_ in CHAINS:
        _require(str(controller.get_source_chain(chain, 'FK Chains')) == chain,
                 f'{path}: {chain} FK mapping changed')
    return asset


def _pose_metrics(animation):
    """Reject collapsed or sideways exported poses throughout each clip."""
    metrics = []
    duration = float(animation.get_play_length())
    for fraction in (0.0, 0.25, 0.5, 0.75):
        sample = animation.get_anim_pose_at_time(duration * fraction, u.AnimPoseEvaluationOptions())
        def point(name):
            return _xyz(u.AnimPoseExtensions.get_bone_pose(sample, name, u.AnimPoseSpaces.WORLD))
        p = {name: point(name) for name in ('chest', 'head', 'lShldr', 'lHand', 'rShldr', 'rHand',
                                           'lThigh', 'lFoot', 'rThigh', 'rFoot')}
        head_height = p['head'][2] - p['chest'][2]
        # A jog may have one raised foot; use the supporting foot.
        torso_height = p['chest'][2] - min(p['lFoot'][2], p['rFoot'][2])
        _require(head_height > 12 and torso_height > 60,
                 f'{animation.get_name()} at {fraction:.2f}: sideways or inverted body')
        for side in ('l', 'r'):
            arm = math.dist(p[side + 'Shldr'], p[side + 'Hand'])
            leg = math.dist(p[side + 'Thigh'], p[side + 'Foot'])
            _require(20 < arm < 120 and 35 < leg < 150,
                     f'{animation.get_name()} at {fraction:.2f}: {side} limb collapsed or exploded '
                     f'({arm:.1f}, {leg:.1f})')
        metrics.append((head_height, torso_height))
    return {'minimum_head_above_chest_cm': round(min(v[0] for v in metrics), 2),
            'minimum_chest_above_supporting_foot_cm': round(min(v[1] for v in metrics), 2),
            'sample_count': len(metrics)}


def build(folder, body, bones):
    """Return (assets, report). Caller saves and independently reloads assets."""
    _contract_check(bones)
    config = json.loads(CONFIG.read_text(encoding='utf8'))
    _require(config.get('schema') == 1, 'invalid RetargetSource.json')
    anim_folder = folder + '/Animations'
    target_rig, target_spans = _rig('IK_Vam', anim_folder, body, 'hip', False)
    assets = [target_rig]
    report = {'schema': 1, 'status': 'target_rig_validated', 'target_rig': target_rig.get_path_name(),
              'target_chain_spans_cm': target_spans, 'clips': {}, 'pose_validation': {}}
    source_mesh = u.load_asset(config['source_mesh'])
    if not source_mesh:
        report['source_missing'] = config['source_mesh']
        return assets, report
    _require(isinstance(source_mesh, u.SkeletalMesh), 'configured source is not a SkeletalMesh')
    clip_paths = config['clips']
    _require(set(clip_paths) == set(CLIP_NAMES), 'clip roles do not match importer contract')
    missing_clips = [path for path in clip_paths.values()
                     if not u.EditorAssetLibrary.find_asset_data(path).is_valid()]
    if missing_clips:
        report['source_missing'] = missing_clips
        return assets, report
    source_rig, source_spans = _rig('IK_Quinn', anim_folder, source_mesh, 'pelvis', True)
    assets.append(source_rig)
    retargeter = _retargeter(anim_folder, source_rig, target_rig, source_mesh, body)
    assets.append(retargeter)
    report.update(status='clips_validated', source_rig=source_rig.get_path_name(),
                  source_chain_spans_cm=source_spans, retargeter=retargeter.get_path_name())
    desired = {role: anim_folder + '/' + name for role, name in CLIP_NAMES.items()}
    existing = {role: u.EditorAssetLibrary.does_asset_exist(path) for role, path in desired.items()}
    _require(not any(existing.values()) or all(existing.values()),
             'partial clip output exists; refusing to overwrite user content')
    if all(existing.values()):
        clips = {role: u.load_asset(path) for role, path in desired.items()}
    else:
        clips = {}
        for role in CLIP_NAMES:
            source_data = u.EditorAssetLibrary.find_asset_data(clip_paths[role])
            _require(source_data.is_valid(), role + ': source clip missing')
            params = u.IKRetargetBatchOperationInputs()
            params.set_editor_property('assets_to_retarget', [source_data])
            params.set_editor_property('source_mesh', source_mesh)
            params.set_editor_property('target_mesh', body)
            params.set_editor_property('ik_retarget_asset', retargeter)
            params.set_editor_property('target_path', anim_folder)
            params.set_editor_property('include_referenced_assets', False)
            params.set_editor_property('search', clip_paths[role].rsplit('/', 1)[-1])
            params.set_editor_property('replace', CLIP_NAMES[role])
            generated = u.IKRetargetBatchOperation.run_batch_retarget(params)
            _require(len(generated) == 1, role + ': UE did not export exactly one clip')
            clips[role] = generated[0].get_asset()
            _require(clips[role] and clips[role].get_path_name().split('.')[0] == desired[role],
                     role + ': unexpected output name or collision')
    for role, clip in clips.items():
        _require(isinstance(clip, u.AnimSequence) and clip.get_editor_property('skeleton') == body.get_editor_property('skeleton'),
                 role + ': wrong target skeleton')
        report['pose_validation'][role] = _pose_metrics(clip)
        report['clips'][role] = clip.get_path_name()
        assets.append(clip)
    return assets, report
