"""Native material ablations; frozen fields, never a production physics setting."""
from pathlib import Path
import subprocess, csv, statistics, json

root = Path(__file__).resolve().parents[1]
out = root / 'Saved/ContactInsights/kernel'
out.mkdir(parents=True, exist_ok=True)
args = [
    'I:/Program/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe',
    'I:/Document/UE5/SmartNPC/Saved/VamBrowserBuild-7a653870e8e9470da44a4fa2a8dbffdc/HostProject/HostProject.uproject',
    '-unattended', '-nosplash', '-NullRHI',
    '-VamBreastTestConfig=/Game/VamRuntime/R_619448199d803edf1ab83af9/RC_Runtime',
    '-ExecCmds=Automation RunTests Vam.Breast.ContactKernel',
    '-TestExit=Automation Test Queue Empty',
    '-VamContactKernelOutput=' + str(out/'timings.csv'),
    '-abslog=' + str(out/'runtime.log')]
with (out/'console.log').open('w') as log:
    code = subprocess.call(args, stdout=log, stderr=subprocess.STDOUT,
                           creationflags=subprocess.CREATE_NO_WINDOW)
if code:
    raise SystemExit(code)
if 'Result={Success} Name={ContactKernel}' not in (out/'runtime.log').read_text(errors='replace'):
    raise SystemExit('ContactKernel did not pass; inspect runtime.log')
rows = list(csv.DictReader((out/'timings.csv').open(encoding='utf-8-sig')))
result = {}
for state in range(3):
    costs = {mode: statistics.median(float(r['elapsed_ms'])*1e6/int(r['calls'])
        for r in rows if int(r['state']) == state and int(r['mode']) == mode)
        for mode in range(4)}
    result[state] = {'ns_per_incidence': costs,
                     'stress_ablation_ns': costs[0]-costs[1],
                     'hessian_ablation_ns': costs[0]-costs[2]}
(out/'results.json').write_text(json.dumps(result, indent=2))
batch_rows = list(csv.DictReader((out/'timings.csv.batch.csv').open(encoding='utf-8-sig')))
batch_result = {k: {'median_12_iterations_ms': statistics.median(float(r['elapsed_ms'])
    for r in batch_rows if int(r['batch']) == k),
    'max_endpoint_error_cm': max(float(r['max_position_error_cm'])
    for r in batch_rows if int(r['batch']) == k)} for k in [5,1,16,32,64,512]}
(out/'batch-results.json').write_text(json.dumps(batch_result, indent=2))
print(json.dumps(result, indent=2))
print(json.dumps(batch_result, indent=2))
