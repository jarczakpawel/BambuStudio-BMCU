#!/usr/bin/env python3
from pathlib import Path
import json
import sys

root = Path(__file__).resolve().parent.parent
manifest = root / 'shared' / 'pjarczak_linux_plugin_bridge_core' / 'method_manifest.json'
if not manifest.exists():
    raise SystemExit('missing manifest')
items = json.loads(manifest.read_text(encoding='utf-8'))
implemented = sum(1 for x in items if x.get('status') == 'implemented')
partial = [x['symbol'] for x in items if x.get('status') == 'partial']
pycache = list(root.rglob('__pycache__'))
pyc = list(root.rglob('*.pyc'))
dup = root / 'pjarczak_bambustudio_linux_bridge_phase7'
print({'total': len(items), 'implemented': implemented, 'partial': len(partial), 'pycache_dirs': len(pycache), 'pyc_files': len(pyc), 'has_nested_phase7_copy': dup.exists()})
if pycache or pyc or dup.exists():
    raise SystemExit(1)
