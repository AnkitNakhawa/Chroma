#!/usr/bin/env python3
"""Rebuild the original abstract sample clip with Chroma (no stock downloads)."""
import argparse
from pathlib import Path
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--chroma', required=True)
a = p.parse_args()
root = Path(__file__).resolve().parent
subprocess.run([a.chroma, 'render', str(root/'demo-footage.json'), '--output', str(root/'demo-footage.mp4')],check=True)
