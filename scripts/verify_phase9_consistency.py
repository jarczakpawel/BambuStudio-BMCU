#!/usr/bin/env python3
from __future__ import annotations

from pathlib import Path
import json
import py_compile
import tempfile

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / 'shared/pjarczak_linux_plugin_bridge_core/method_manifest.json').read_text())
summary = (root / 'docs/method-status-summary.md').read_text()
host_cpp = (root / 'tools/pjarczak_bambu_linux_host/LinuxPluginHost.cpp').read_text()
state_cpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderState.cpp').read_text()

for script in sorted((root / 'scripts').glob('*.py')):
    with tempfile.TemporaryDirectory() as td:
        py_compile.compile(str(script), cfile=str(Path(td) / (script.stem + '.pyc')), doraise=True)

partial = [x['symbol'] for x in manifest if x.get('status') == 'partial']
if partial:
    raise SystemExit(f'manifest still contains partial entries: {partial}')

if '- partial: 0' not in summary:
    raise SystemExit('summary does not report partial: 0')

required = [
    'bridge.callback_reply',
    'register_callback_request',
    'set_callback_reply',
    'callback.get_country_code',
    'agent->get_country_code',
]
combined = host_cpp + '\n' + state_cpp
for snippet in required:
    if snippet not in combined:
        raise SystemExit(f'missing phase9 callback snippet: {snippet}')

print({'total': len(manifest), 'implemented': len(manifest), 'partial': 0, 'todo': sum(1 for x in manifest if x.get('status') == 'todo')})
