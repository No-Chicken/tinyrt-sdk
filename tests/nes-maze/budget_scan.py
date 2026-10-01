"""Find a passing budget only for this finite maze trace; never a ROM-wide bound."""
import argparse, hashlib, json, subprocess
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--runner',required=True,type=Path)
    p.add_argument('--wasm',required=True,type=Path)
    p.add_argument('--output',required=True,type=Path)
    args=p.parse_args();trials=[]
    def run(budget,extra=0):
        result=subprocess.run([str(args.runner),str(args.wasm),str(budget),str(extra)],capture_output=True,text=True,timeout=90)
        trial={'budget':budget,'extra_frames_per_instance':extra,'returncode':result.returncode,'output':result.stdout.strip(),'stderr':result.stderr.strip()}
        trials.append(trial)
        if result.returncode and 'instruction limit exceeded' not in result.stdout:
            raise RuntimeError(json.dumps(trial))
        return result.returncode==0
    if not run(100000):raise RuntimeError('manifest budget does not pass')
    low,high=1,100000
    while low<high:
        mid=(low+high)//2
        if run(mid):high=mid
        else:low=mid+1
    assert run(low) and (low==1 or not run(low-1))
    assert run(100000,260)
    result={'scope':'Finite original maze trace only; desktop WAMR, not device FPS or arbitrary-ROM bound.',
            'wasm_sha256':hashlib.sha256(args.wasm.read_bytes()).hexdigest(),
            'minimum_passing_budget':low,'manifest_budget':100000,'trials':trials}
    args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps({'minimum_passing_budget':low,'trials':len(trials),'output':str(args.output)}),flush=True)
if __name__=='__main__':main()
