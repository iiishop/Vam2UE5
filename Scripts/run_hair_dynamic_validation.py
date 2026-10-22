"""Serial, resumable orchestration of existing real UE hair verification tools."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--styles', nargs='+', choices=['long', 'ponytail'], default=['long', 'ponytail'])
p.add_argument('--tag',default='final')
p.add_argument('--replays-dir',type=Path)
p.add_argument('--steps', nargs='+', choices=['capture', 'editor', 'pie', 'benchmark', 'baseline-benchmark'],
               default=['capture', 'editor', 'pie', 'benchmark', 'baseline-benchmark'])
a = p.parse_args()
assert a.tag and all(c.isalnum() or c=='-' for c in a.tag)
root = Path(__file__).resolve().parents[1]
runs = root / 'Saved/HairDynamicRepair'
project = 'I:/Document/UE5/SmartNPC/SmartNPC.uproject'

def invoke(args, log):
    with log.open('w', encoding='utf-8') as stream:
        result = subprocess.run([sys.executable, '-X', 'utf8', *map(str, args)], cwd=root,
                                stdout=stream, stderr=subprocess.STDOUT)
    if result.returncode:
        raise RuntimeError(f'{args[0]} failed ({result.returncode}); see {log}')

for step in a.steps:
    for style in a.styles:
        candidate = runs / f'{style}-prediction-{a.tag}'
        bundle = candidate / 'bundle.json'
        assert json.loads(bundle.read_text())['independent_reload_verified']
        replay = (a.replays_dir or root/'Saved/HairDynamicUE/inputs-v2') / f'{style}.json'
        out = candidate / step
        if step == 'baseline-benchmark':
            bundle = root / f'Saved/HairGeneric/{style}-r8/bundle.json'
            out = runs / f'baseline-{style}' / 'benchmark'
        print(f'{style}: {step}', flush=True)
        completion = out / ('check.json' if step == 'editor' else
                            'summary.json' if 'benchmark' in step else 'metrics.json')
        if completion.exists():
            d = json.loads(completion.read_text())
            if step == 'editor': assert d['status'] == 'passed'
            elif 'benchmark' not in step: assert d['head_replay_validation']['status'] == 'PASS'
            print(f'  existing completed result: {completion}', flush=True)
            continue
        if step == 'editor':
            invoke(['Scripts/run_hair_editor_check.py', bundle, out], candidate / 'editor-runner.log')
        elif 'benchmark' in step:
            invoke(['Scripts/run_hair_capture.py', bundle, out, '--benchmark', '--project', project],
                   candidate / f'{step}-runner.log')
            invoke(['Scripts/summarize_hair_benchmark.py', out], candidate / f'{step}-summary.log')
        else:
            args = ['Scripts/run_hair_capture.py', bundle, out, '--source', replay, '--project', project]
            if step == 'pie': args.append('--pie')
            invoke(args, candidate / f'{step}-runner.log')
            invoke(['Scripts/audit_hair_dynamic_comparison.py', out / 'capture.json',
                    completion, '--replay', replay], candidate / f'{step}-metrics.log')
        assert completion.exists(), str(completion)
print('Requested real UE validation stages completed.', flush=True)
