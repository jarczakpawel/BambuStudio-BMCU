#!/usr/bin/env python3
from __future__ import annotations

from collections import Counter
from pathlib import Path
import csv
import json
import py_compile
import tempfile
import re

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / 'shared/pjarczak_linux_plugin_bridge_core/method_manifest.json').read_text())
summary = (root / 'docs/method-status-summary.md').read_text()
csv_rows = list(csv.DictReader((root / 'docs/method-status.csv').open(newline='')))
forwarder = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderExports.cpp').read_text()
state_hpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderState.hpp').read_text()
state_cpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderState.cpp').read_text()
host_cpp = (root / 'tools/pjarczak_bambu_linux_host/LinuxPluginHost.cpp').read_text()
event_pump = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxSoBridgeEventPump.cpp').read_text()

for script in sorted((root / 'scripts').glob('*.py')):
    with tempfile.TemporaryDirectory() as td:
        py_compile.compile(str(script), cfile=str(Path(td) / (script.stem + '.pyc')), doraise=True)

for path in list((root / 'src').rglob('*.cpp')) + list((root / 'src').rglob('*.hpp')) + list((root / 'tools').rglob('*.cpp')) + list((root / 'tools').rglob('*.hpp')):
    if b'\x00' in path.read_bytes():
        raise SystemExit(f'NUL byte found in {path}')

counts = Counter(item['status'] for item in manifest)
for key in ['implemented', 'partial', 'todo']:
    m = re.search(rf'- {key}: (\d+)', summary)
    if not m:
        raise SystemExit(f'missing summary count for {key}')
    if int(m.group(1)) != counts.get(key, 0):
        raise SystemExit(f'summary mismatch for {key}: summary={m.group(1)} manifest={counts.get(key, 0)}')

if len(csv_rows) != len(manifest):
    raise SystemExit(f'csv row count mismatch: csv={len(csv_rows)} manifest={len(manifest)}')

required = [
    'json_to_nested_string_map',
    'invoke_progress_check_job',
    'register_remote_tunnel',
    'dispatch_tunnel_event',
    'queue_tunnel_event',
    'net.get_user_presets',
    'net.get_setting_list',
    'net.get_setting_list2',
    'net.get_subtask',
    'net.put_model_mall_rating',
    'net.get_oss_config',
    'net.put_rating_picture_oss',
    'net.get_model_mall_rating',
    'src.set_logger',
    'Bambu_SetLogger',
]
combined = '\n'.join([forwarder, state_hpp, state_cpp, host_cpp, event_pump])
for snippet in required:
    if snippet not in combined:
        raise SystemExit(f'missing required snippet: {snippet}')

if re.search(r'PJBRIDGE_EXPORT .*unsupported\(\)', forwarder):
    raise SystemExit('forwarder still contains exported unsupported() stubs')

if counts.get('todo', 0) != 0:
    raise SystemExit(f'manifest still contains todo entries: {counts.get("todo", 0)}')

print({'total': len(manifest), **counts})
