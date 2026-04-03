#!/usr/bin/env python3
import json, re
from pathlib import Path
root = Path(__file__).resolve().parents[1]
json_items = json.loads((root / 'shared/pjarczak_linux_plugin_bridge_core/method_manifest.json').read_text())
json_map = {x['symbol']: x for x in json_items}
text = (root / 'shared/pjarczak_linux_plugin_bridge_core/BridgeCoreMethodManifest.cpp').read_text()
pat = re.compile(r'MethodManifestEntry\{"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)",\s*"([^"]+)"\}')
cpp_map = {m.group(1): {'symbol': m.group(1), 'exported_name': m.group(2), 'area': m.group(3), 'status': m.group(4), 'stability': m.group(5), 'notes': m.group(6)} for m in pat.finditer(text)}
if set(json_map) != set(cpp_map):
    raise SystemExit('symbol set mismatch between json manifest and cpp manifest')
for sym in sorted(json_map):
    a = json_map[sym]
    b = cpp_map[sym]
    for key in ['exported_name', 'area', 'status', 'stability', 'notes']:
        if a[key] != b[key]:
            raise SystemExit(f'mismatch for {sym} field {key}: json={a[key]!r} cpp={b[key]!r}')
print({'total': len(json_map), 'synced': True})
