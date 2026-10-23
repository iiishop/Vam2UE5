"""Validate the stored UE runs and media; does not substitute for running UE."""
import hashlib
import json
import math
import shutil
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SAVED = ROOT / 'Saved/HairStructureRepair'
OUT = ROOT / 'Evidence/HairStructureRepair'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    metrics = read(OUT / 'metrics.json')
    assert set(metrics) == {'wavy', 'long', 'ponytail', 'bun'}
    result = {'scope': 'Recorded UE build, regenerated assets, Editor/PIE evidence and media integrity',
              'baseline_commit': 'f726326', 'styles': {}, 'videos': {}}
    log = SAVED / 'compile-04.log'
    assert 'Result: Succeeded' in log.read_text(errors='replace')
    result['cpp_build'] = {'status': 'passed', 'log': str(log)}
    tests = SAVED / 'python-tests-final.log'
    test_text = tests.read_text(encoding='utf-8-sig')
    assert 'Ran 42 tests' in test_text and test_text.rstrip().endswith('OK')
    result['python_tests'] = {'passed': 42, 'log': str(tests)}
    local_dll = ROOT / 'Binaries/Win64/UnrealEditor-VamResourceBrowser.dll'
    host_dll = ROOT.parents[1] / 'Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/Plugins/VamResourceBrowser/Binaries/Win64/UnrealEditor-VamResourceBrowser.dll'
    assert digest(local_dll) == digest(host_dll)
    result['deployed_dll_sha256'] = digest(local_dll)
    for name, row in metrics.items():
        run = SAVED / (name + '-buffered')
        bundle = read(run / 'bundle.json')
        assert bundle['independent_reload_verified'] and bundle['status'] == 'committed'
        assert row['source_groups_unchanged'] and row['matrix_validation'] == 'PASS'
        assert row['shader_validation']['passed'] and not row['shader_validation']['errors']
        assert row['frames'] == (720 if name == 'long' else 719)
        editor = row['editor']
        assert editor['status'] == 'passed' and len(editor['gpu_samples']) == 2
        assert 'frustum' in editor['scope']
        for group in editor['rows']:
            assert group['head_attachment_error_cm'] < .01
            assert all(p['self_collision_viscosity'] == 0 for p in group['parameters'])
        snapshots = []
        for file in editor['gpu_samples']:
            gpu = read(Path(file))
            assert gpu['world'] == 'Editor'
            groups = []
            for group in gpu['groups']:
                assert group['readback_ready'] and group['render_readback_ready']
                bounds = {}
                for key in ['guide_points', 'render_points', 'render_rest_points']:
                    points = group[key]
                    assert points and all(math.isfinite(x) for p in points for x in p)
                    bounds[key] = {'min': [min(p[i] for p in points) for i in range(3)],
                                   'max': [max(p[i] for p in points) for i in range(3)]}
                groups.append(bounds)
            snapshots.append({'file': file, 'sha256': digest(Path(file)), 'bounds_cm': groups})
        package = bundle['blueprint'].split('.')[0].removeprefix('/Game/')
        asset = ROOT.parents[1] / 'Content' / (package + '.uasset')
        assert asset.is_file()
        result['styles'][name] = {'identity': row['identity'], 'blueprint': bundle['blueprint'],
                                 'blueprint_sha256': digest(asset), 'editor': 'passed',
                                 'editor_snapshots': snapshots, 'pie_frames': row['frames'],
                                 'head_matrix_replay': row['matrix_validation'],
                                 'shader': 'passed', 'source_geometry_constraints_unchanged': True}
    interpolation = read(SAVED / 'interpolation-asset-check.json')
    assert interpolation['wavy-buffered']['use_unique_guide'] == [True]
    assert interpolation['wavy-multi-guide']['use_unique_guide'] == [False]
    assert read(SAVED / 'wavy-multi-guide/pie/shader-validation.json')['passed']
    result['interpolation_asset_reload'] = interpolation
    for name in [*metrics, 'interpolation']:
        video = OUT / (name + '-comparison.mp4')
        probe = subprocess.check_output([shutil.which('ffprobe'), '-v', 'error', '-select_streams', 'v:0',
                                        '-show_entries', 'stream=codec_name,width,height,nb_frames,duration',
                                        '-of', 'json', str(video)], text=True)
        stream = json.loads(probe)['streams'][0]
        assert (stream['width'], stream['height']) == (1920, 580)
        assert int(stream['nb_frames']) == (720 if name == 'long' else 719)
        result['videos'][name] = dict(stream, sha256=digest(video),
                                     historical_left=name != 'interpolation')
    result['retained_failures'] = [
        {'path': str(SAVED / 'wavy-baseline/pie/shader-validation.json'), 'reason': 'Old baked asset X4600; not a valid fresh baseline'},
        {'path': str(SAVED / 'wavy-no-support'), 'reason': 'Ablation still exceeded constant table limit'},
        {'path': str(SAVED / 'wavy-no-empty-links'), 'reason': 'Ablation still exceeded constant table limit'},
        {'path': str(SAVED / 'compile-03.log'), 'reason': 'DLL lock; subsequent compile-03-retry and compile-04 succeeded'},
        {'path': str(SAVED / 'bun-buffered/editor'), 'reason': 'Culled render groups contained uninitialized data; diagnostic culling-off rerun retained separately'}]
    result['limitations'] = [
        'Head-motion orbit sequence only; no new locomotion recording or new VaM dynamic sampling.',
        'Historical first-64/last-64 failure boundary has not been traced to a particular driver instruction.',
        'Guide proxy depth does not prove visible strand versus actual skin nonpenetration.',
        'Editor all-group readback disables interpolation frustum culling only in that diagnostic process.',
        'Browser automation denied file:// protocol; HTML/media references checked locally, no browser screenshot QA claimed.',
        '128-guide budget, sparse source links and material timing remain approximations.']
    result['status'] = 'passed_for_stated_scope'
    (OUT / 'validation.json').write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf8')
    print(json.dumps({'status': result['status'], 'styles': list(result['styles']),
                      'videos': {k: v['nb_frames'] for k, v in result['videos'].items()}}, indent=2))


if __name__ == '__main__':
    main()
