#!/usr/bin/env python3
"""Sequential alternating full-showcase cache A/B benchmark; never runs renders concurrently."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import re
import statistics
import subprocess
import time


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--chroma',type=Path,required=True)
    p.add_argument('--composition',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--runs',type=int,default=3)
    args=p.parse_args()
    if args.runs < 2: p.error('at least two runs per variant are required')
    args.output.mkdir(parents=True,exist_ok=True)
    samples=[]
    for index in range(args.runs):
        order=['cached','uncached'] if index%2==0 else ['uncached','cached']
        for variant in order:
            name=f'{variant}-{index+1}'
            profile=args.output/f'{name}.json'
            command=[str(args.chroma.resolve()),'benchmark',str(args.composition.resolve()),'--iterations','1','--output',str(profile.resolve())]
            if variant=='uncached': command += ['--image-cache-bytes','0','--text-cache-bytes','0']
            timed=['/usr/bin/time','-l' if platform.system()=='Darwin' else '-v',*command]
            start=time.perf_counter()
            with (args.output/f'{name}.log').open('w') as log:
                subprocess.run(timed,stdout=log,stderr=log,check=True)
            wall=time.perf_counter()-start
            text=(args.output/f'{name}.log').read_text()
            pattern=r'(\d+)\s+maximum resident set size' if platform.system()=='Darwin' else r'Maximum resident set size \(kbytes\):\s*(\d+)'
            match=re.search(pattern,text)
            if not match: raise RuntimeError('Unable to parse peak RSS from time output')
            rss=int(match[1])*(1 if platform.system()=='Darwin' else 1024)
            data=json.loads(profile.read_text())['iterations'][0]
            if data['frames_encoded'] != 540 or data['audio_samples_encoded'] != 864000:
                raise RuntimeError('Benchmark did not render the complete showcase')
            samples.append(dict(variant=variant,run=index+1,wall_seconds=wall,peak_rss_bytes=rss,profile=data))
            report=dict(platform=platform.platform(),processor=platform.processor(),
                        composition_sha256=hashlib.sha256(args.composition.read_bytes()).hexdigest(),
                        note='Fresh render process/context each sample; warm filesystem; sequential alternating order. CPU only. No GPU or competitor comparison.',samples=samples)
            (args.output/'results.json').write_text(json.dumps(report,indent=2)+'\n')
            print(f'{name}: {wall:.2f}s wall, {540/wall:.2f} fps, {rss/1024**2:.1f} MiB peak RSS',flush=True)
    for variant in ('cached','uncached'):
        group=[s['wall_seconds'] for s in samples if s['variant']==variant]
        print(f'{variant}: median {statistics.median(group):.2f}s, range {min(group):.2f}–{max(group):.2f}s',flush=True)

if __name__=='__main__': main()
