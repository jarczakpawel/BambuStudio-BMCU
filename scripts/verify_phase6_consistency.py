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
manifest_path = root / 'shared/pjarczak_linux_plugin_bridge_core/method_manifest.json'
manifest = json.loads(manifest_path.read_text())
manifest_by_symbol = {item['symbol']: item for item in manifest}
summary_path = root / 'docs/method-status-summary.md'
csv_path = root / 'docs/method-status.csv'
forwarder = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderExports.cpp').read_text()
state_hpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderState.hpp').read_text()
host_cpp = (root / 'tools/pjarczak_bambu_linux_host/LinuxPluginHost.cpp').read_text()

# 1. script syntax
for script in sorted((root / 'scripts').glob('*.py')):
    with tempfile.TemporaryDirectory() as td:
        py_compile.compile(str(script), cfile=str(Path(td) / (script.stem + '.pyc')), doraise=True)

# 2. no NUL bytes in text files
for path in list((root / 'src').rglob('*.cpp')) + list((root / 'src').rglob('*.hpp')) + list((root / 'tools').rglob('*.cpp')) + list((root / 'tools').rglob('*.hpp')):
    data = path.read_bytes()
    if b'\x00' in data:
        raise SystemExit(f'NUL byte found in {path}')

# 3. summary counts must match manifest
counts = Counter(item['status'] for item in manifest)
summary = summary_path.read_text()
for key in ['implemented', 'partial', 'todo']:
    m = re.search(rf'- {key}: (\d+)', summary)
    if not m:
        raise SystemExit(f'missing summary count for {key}')
    if int(m.group(1)) != counts.get(key, 0):
        raise SystemExit(f'summary count mismatch for {key}: summary={m.group(1)} manifest={counts.get(key, 0)}')

# 4. csv must match manifest size
with csv_path.open(newline='') as f:
    rows = list(csv.DictReader(f))
if len(rows) != len(manifest):
    raise SystemExit(f'csv row count mismatch: csv={len(rows)} manifest={len(manifest)}')

# 5. structural guards
required_snippets = [
    'struct BridgeJobState;',
    'PJarczakLinuxBridgeCompat.hpp',
    'std::string env_or(',
    'bambu_network_request_setting_id',
    'net.get_camera_url',
    'src.get_stream_info',
    'src.recv_message',
]
combined = state_hpp + '\n' + forwarder + '\n' + host_cpp
for snippet in required_snippets:
    if snippet not in combined:
        raise SystemExit(f'required snippet missing: {snippet}')


# 5b. job-state helpers must exist in state cpp
state_cpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderState.cpp').read_text()
for snippet in ['std::shared_ptr<BridgeJobState> register_job_state', 'std::shared_ptr<BridgeJobState> find_job_state', 'void unregister_job_state']:
    if snippet not in state_cpp:
        raise SystemExit(f'job-state helper missing in state cpp: {snippet}')
# 6. implemented methods should not still have explicit unsupported forwarder stubs for the same symbol
implemented_symbols = [item['symbol'] for item in manifest if item['status'] == 'implemented']
for symbol in implemented_symbols:
    pat = re.escape(symbol) + r'[^\n]*unsupported\(\)'
    if re.search(pat, forwarder):
        raise SystemExit(f'implemented symbol still calls unsupported(): {symbol}')

# 7. selected method coverage
must_be_implemented = {
    'bambu_network_start_print',
    'bambu_network_start_publish',
    'bambu_network_get_camera_url',
    'bambu_network_request_setting_id',
    'bambu_network_get_my_message',
    'Bambu_GetStreamInfo',
    'Bambu_RecvMessage',
    'Bambu_Init',
    'Bambu_Deinit',
}
missing = [s for s in must_be_implemented if manifest_by_symbol.get(s, {}).get('status') != 'implemented']
if missing:
    raise SystemExit('selected methods not implemented: ' + ', '.join(missing))

print({'total': len(manifest), **counts})
