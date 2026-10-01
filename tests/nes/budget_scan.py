"""Measure the minimum passing budget for the finite N1 test, in fresh processes.

A failed process is discarded. Neither guest nor runtime resumes after a trap.
This is evidence for this fixture/trace, not a worst-case bound for arbitrary ROMs.
"""
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--runner', required=True, type=Path)
p.add_argument('--wasm', required=True, type=Path)
p.add_argument('--output', required=True, type=Path)
a = p.parse_args()
trials = []
def passes(budget):
    result = subprocess.run([str(a.runner.resolve()), str(a.wasm.resolve()), str(budget)],
                            capture_output=True, text=True, timeout=60)
    ok = result.returncode == 0 and 'WASM PASS' in result.stdout
    trials.append({'budget': budget, 'pass': ok, 'exit_code': result.returncode,
                   'output': result.stdout.strip(), 'stderr': result.stderr.strip()})
    print(f'budget={budget} pass={ok}', flush=True)
    return ok

low, high = 1, 100000
if not passes(high):
    raise SystemExit('N1 fails at policy ceiling; no threshold measured')
while low < high:
    middle = (low + high) // 2
    if passes(middle): high = middle
    else: low = middle + 1
result = {'scope': 'two fresh instances, frame 13 plus frame 8, A latch on/off, init/event/render',
          'minimum_passing_budget_for_trace': high, 'policy_budget': 100000, 'trials': trials}
a.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
print(json.dumps({k:v for k,v in result.items() if k != 'trials'}))
