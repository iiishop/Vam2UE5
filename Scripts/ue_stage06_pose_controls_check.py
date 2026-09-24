"""Editor-host acceptance for the Stage06 on-character pose control contract.

Run with ExecutePythonScript after compiling the runtime module and upgrading the
Stage06 rig asset. The two Blueprint instances are loaded asynchronously; this
script writes a JSON result and closes the editor when its checks complete.
"""

import json
import math
import time
import traceback
from pathlib import Path

import unreal as u


REPORT = (Path(u.Paths.project_dir()) /
          'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-pose-controls-check.json')
BLUEPRINT = '/Game/VamStage06/BP_VamCharacter'
TORSO = ('pelvis', 'spine', 'spine_upper', 'chest', 'neck', 'head')
LIMB_PARTS = ('clavicle', 'shoulder', 'elbow', 'hand', 'hip', 'knee',
              'foot', 'toe', 'big_toe')
FINGERS = ('thumb', 'index', 'mid', 'ring', 'pinky')
EXCLUDED = ('root', 'jaw', 'eye_l', 'eye_r')


def save(result):
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')


def required_semantics():
    result = list(TORSO)
    for side in ('left', 'right'):
        result.extend(f'{side}_{part}' for part in LIMB_PARTS)
        result.extend(f'{side}_{finger}_{segment}'
                      for finger in FINGERS for segment in (1, 2, 3))
    return result


def bone_transform(body, bone):
    return body.get_socket_transform(bone, u.RelativeTransformSpace.RTS_WORLD)


def rotation_difference_degrees(a, b):
    qa, qb = a.rotation, b.rotation
    dot = abs(qa.x * qb.x + qa.y * qb.y + qa.z * qb.z + qa.w * qb.w)
    return math.degrees(2 * math.acos(min(1.0, max(0.0, dot))))


def check(actors, characters, motions, interactions):
    bodies = [c.get_editor_property('body') for c in characters]
    first, second = bodies
    rig = characters[0].get_editor_property('rig_profile')
    assert rig, 'First character has no rig profile'
    mapped_joints = list(rig.get_editor_property('joints'))
    joints = {str(j.get_editor_property('semantic')): j for j in mapped_joints}
    required = required_semantics()
    assert len(joints) == len(mapped_joints), 'Duplicate semantic keys in rig profile'
    assert all(name in joints for name in required), sorted(set(required) - set(joints))

    controlled = {}
    for semantic in required:
        bone = str(joints[semantic].get_editor_property('bone'))
        index = first.get_bone_index(bone)
        assert index >= 0, (semantic, bone, index)
        assert characters[0].is_pose_control_bone(index), (semantic, bone, index)
        assert characters[1].is_pose_control_bone(second.get_bone_index(bone))
        controlled[semantic] = {'bone': bone, 'index': index}

    for semantic in EXCLUDED:
        assert semantic in joints, f'Missing exclusion mapping: {semantic}'
        bone = str(joints[semantic].get_editor_property('bone'))
        index = first.get_bone_index(bone)
        assert index >= 0, (semantic, bone, index)
        assert not characters[0].is_pose_control_bone(index), (semantic, bone, index)
        assert not characters[0].set_pose_control_rotation(
            index, u.Rotator(pitch=0, yaw=20, roll=0))
    assert not characters[0].is_pose_control_bone(-1)

    # The waist must use the same finite rig bounds as the final IK output.
    waist = controlled['spine_upper']
    waist_joint = joints['spine_upper']
    maximum = waist_joint.get_editor_property('maximum')
    minimum = waist_joint.get_editor_property('minimum')
    assert waist_joint.get_editor_property('limit_rotation')
    before_head = bone_transform(first, controlled['head']['bone'])
    other_head = bone_transform(second, controlled['head']['bone'])
    assert characters[0].set_pose_control_rotation(
        waist['index'], u.Rotator(pitch=0, yaw=170, roll=0))
    clamped = characters[0].get_pose_control_rotation(waist['index'])
    assert minimum.yaw - 0.1 <= clamped.yaw <= maximum.yaw + 0.1, (clamped, minimum, maximum)
    assert abs(clamped.yaw - maximum.yaw) < 0.2, (clamped, maximum)
    changed_head = bone_transform(first, controlled['head']['bone'])
    head_angle = rotation_difference_degrees(before_head, changed_head)
    other_head_angle = rotation_difference_degrees(other_head,
                         bone_transform(second, controlled['head']['bone']))
    assert head_angle > 5.0, head_angle
    assert other_head_angle < 0.2, other_head_angle
    characters[0].reset_pose_control_rotations()

    # A local shoulder rotation must visibly move its descendant hand while the
    # second instance and its own pose map stay untouched.
    hand_bone = controlled['left_hand']['bone']
    hand_before = bone_transform(first, hand_bone).translation
    other_hand_before = bone_transform(second, hand_bone).translation
    shoulder = controlled['left_shoulder']
    assert characters[0].set_pose_control_rotation(
        shoulder['index'], u.Rotator(pitch=0, yaw=30, roll=0))
    hand_movement = (bone_transform(first, hand_bone).translation - hand_before).length()
    other_movement = (bone_transform(second, hand_bone).translation - other_hand_before).length()
    assert hand_movement > 1.0, hand_movement
    assert other_movement < 0.1, other_movement
    other_rotation = characters[1].get_pose_control_rotation(shoulder['index'])
    assert max(abs(other_rotation.pitch), abs(other_rotation.yaw),
               abs(other_rotation.roll)) < 0.01, other_rotation
    characters[0].reset_pose_control_rotations()
    reset_error = (bone_transform(first, hand_bone).translation - hand_before).length()
    assert reset_error < 0.2, reset_error

    pelvis_bone = controlled['pelvis']['bone']
    spine_bone = controlled['spine']['bone']
    assert interactions[0].set_root_motion_response(pelvis_bone, spine_bone)
    mode = str(interactions[0].get_editor_property('mode')).upper()
    assert 'LOCAL_RESPONSE' in mode, mode
    root_bone = str(joints['root'].get_editor_property('bone'))
    simulating = {
        'lower_branch': bool(first.is_simulating_physics(pelvis_bone)),
        'upper_branch': bool(first.is_simulating_physics(spine_bone)),
        'actor_root': bool(first.is_simulating_physics(root_bone)),
    }
    assert simulating['lower_branch'], (pelvis_bone, simulating)
    assert simulating['upper_branch'], (spine_bone, simulating)
    assert not simulating['actor_root'], (root_bone, simulating)

    # Root translation goes through Motion, not through a bone pose handle.
    motions[0].set_preview_paused(True)
    motions[1].set_preview_paused(True)
    root = actors[0].get_actor_transform()
    timestamp = u.SystemLibrary.get_game_time_in_seconds(actors[0]) + 1.0
    motions[0].move_continuously(root, timestamp)
    next_root = u.Transform(location=root.translation + u.Vector(30, 0, 0),
                            rotation=root.rotation.rotator())
    motions[0].move_continuously(next_root, timestamp + 0.1)
    speed = motions[0].get_motion().linear_velocity.length()
    other_speed = motions[1].get_motion().linear_velocity.length()
    assert speed > 100.0, speed
    assert other_speed < 0.01, other_speed
    assert (actors[0].get_actor_location() - next_root.translation).length() < 0.1
    assert (actors[1].get_actor_location() - u.Vector(0, 220, 0)).length() < 0.1

    return {
        'status': 'passed',
        'blueprint': BLUEPRINT,
        'controlled_joint_count': len(controlled),
        'controlled_semantics': required,
        'excluded_semantics': list(EXCLUDED),
        'waist_requested_yaw_deg': 170,
        'waist_clamped_yaw_deg': clamped.yaw,
        'waist_rig_max_yaw_deg': maximum.yaw,
        'head_rotation_from_waist_deg': head_angle,
        'other_head_rotation_deg': other_head_angle,
        'hand_movement_from_shoulder_cm': hand_movement,
        'other_hand_movement_cm': other_movement,
        'hand_reset_error_cm': reset_error,
        'root_motion_mode': mode,
        'physics_branch_simulation': simulating,
        'root_linear_speed_cm_s': speed,
        'other_root_linear_speed_cm_s': other_speed,
    }


u.EditorPythonScripting.set_keep_python_script_alive(True)
try:
    u.EditorLoadingAndSavingUtils.new_blank_map(False)
    subsystem = u.get_editor_subsystem(u.EditorActorSubsystem)
    blueprint = u.load_asset(BLUEPRINT)
    assert blueprint, BLUEPRINT
    actors = [subsystem.spawn_actor_from_class(blueprint.generated_class(), u.Vector(0, y, 0))
              for y in (0, 220)]
    assert all(actors)
    characters = [a.get_editor_property('character') for a in actors]
    motions = [a.get_editor_property('motion') for a in actors]
    interactions = [a.get_editor_property('interaction') for a in actors]
    assert all(characters) and all(motions) and all(interactions)
    for character in characters:
        character.load_character()
    started = time.monotonic()
except Exception:
    error = traceback.format_exc()
    save({'status': 'failed', 'error': error})
    u.log_error(error)
    u.SystemLibrary.quit_editor()
    raise


def tick(_delta_seconds):
    try:
        assert time.monotonic() - started < 90, 'Stage06 Blueprint instances did not finish loading'
        bodies = [c.get_editor_property('body') for c in characters]
        if not all(body and body.get_anim_instance() for body in bodies):
            return
        result = check(actors, characters, motions, interactions)
    except Exception:
        result = {'status': 'failed', 'error': traceback.format_exc()}
        u.log_error(result['error'])
    save(result)
    u.unregister_slate_post_tick_callback(handle)
    u.SystemLibrary.quit_editor()


handle = u.register_slate_post_tick_callback(tick)
