"""Serialize pose-control bounds on the Stage06 rig without changing its mapping.

Run with UnrealEditor-Cmd -run=pythonscript -script=<this file> after the
runtime module containing FVamRigJoint.bPoseControl has been compiled.
"""
import json
import re
import traceback
from pathlib import Path

import unreal as u


ROOT = '/Game/VamStage06'
PROJECT = Path(u.Paths.project_dir())
REPORT = PROJECT / 'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-pose-rig-upgrade.json'


def bounds_for_semantic(semantic):
    """Return symmetric Pitch, Yaw, Roll bounds in degrees for mapped pose joints."""
    # The source rig has not yet been calibrated for one-sided bend directions.
    # Finite symmetric bounds are deterministic without assuming an axis sign.
    torso = {
        'pelvis': (25, 25, 25),
        'spine': (25, 25, 25),
        'spine_upper': (30, 30, 30),
        'chest': (35, 35, 35),
        'neck': (35, 35, 35),
        'head': (40, 40, 40),
    }
    if semantic in torso:
        return torso[semantic]
    match = re.fullmatch(r'(left|right)_(.+)', semantic)
    if not match:
        return None
    part = match.group(2)
    if part in ('toe', 'big_toe'):
        return (35, 25, 25)
    if re.fullmatch(r'(thumb|index|mid|ring|pinky)_1', part):
        return (60, 25, 25)
    if re.fullmatch(r'(thumb|index|mid|ring|pinky)_[23]', part):
        return (60, 15, 15)
    if part in ('elbow', 'knee'):
        return (115, 50, 50)
    if part in ('shoulder', 'hip'):
        return (100, 100, 100)
    if part in ('clavicle', 'hand', 'foot'):
        return (65, 65, 65)
    return None


def configure_joint(joint, semantic):
    """Set only the control policy; leave bone mapping and preferred bend intact."""
    bounds = bounds_for_semantic(semantic)
    joint.set_editor_property('pose_control', bounds is not None)
    if bounds is None:
        return False
    joint.set_editor_property('limit_rotation', True)
    pitch, yaw, roll = bounds
    joint.set_editor_property('minimum', u.Rotator(pitch=-pitch, yaw=-yaw, roll=-roll))
    joint.set_editor_property('maximum', u.Rotator(pitch=pitch, yaw=yaw, roll=roll))
    return True


def upgrade_rig(rig):
    joints = list(rig.get_editor_property('joints'))
    controlled = []
    for joint in joints:
        semantic = str(joint.get_editor_property('semantic'))
        if configure_joint(joint, semantic):
            controlled.append(semantic)
    rig.set_editor_property('joints', joints)
    assert u.EditorAssetLibrary.save_loaded_asset(rig), rig.get_path_name()
    return {'rig': rig.get_path_name(), 'mapped': len(joints),
            'controlled': len(controlled), 'semantics': controlled}


def run():
    paths = {ROOT + '/DA_Rig_Eyelids_a11a18a6'}
    adapter_report = PROJECT / 'Plugins/VamResourceBrowser/Saved/NativeBuild/stage06-blink-adapter.json'
    if adapter_report.exists():
        latest = json.loads(adapter_report.read_text(encoding='utf8'))
        if latest.get('status') == 'complete' and latest.get('rig'):
            paths.add(latest['rig'])
    results = []
    for path in sorted(paths):
        rig = u.load_asset(path)
        assert rig, path
        results.append(upgrade_rig(rig))
    REPORT.parent.mkdir(parents=True, exist_ok=True)
    REPORT.write_text(json.dumps({'status': 'complete', 'rigs': results},
                                 ensure_ascii=False, indent=2), encoding='utf8')
    return results


if __name__ == '__main__':
    try:
        run()
    except Exception:
        REPORT.parent.mkdir(parents=True, exist_ok=True)
        REPORT.write_text(json.dumps({'status': 'failed', 'traceback': traceback.format_exc()},
                                     ensure_ascii=False, indent=2), encoding='utf8')
        raise
