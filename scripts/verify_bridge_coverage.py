#!/usr/bin/env python3
from pathlib import Path
import json
import csv
import re

root = Path(__file__).resolve().parent.parent
manifest = json.loads((root / "shared/pjarczak_linux_plugin_bridge_core/method_manifest.json").read_text())
exports = (root / "src/slic3r/Utils/PJarczakLinuxBridge/PJarczakBambuNetworkForwarderExports.cpp").read_text()

symbols = {m["symbol"]: m for m in manifest}
checks = [
    "bambu_network_bind",
    "bambu_network_start_print",
    "bambu_network_start_local_print_with_record",
    "bambu_network_start_send_gcode_to_sdcard",
    "bambu_network_start_local_print",
    "bambu_network_start_sdcard_print",
    "bambu_network_start_publish",
]
for name in checks:
    if name not in exports:
        raise SystemExit(f"missing export: {name}")

status_counts = {}
for item in manifest:
    status_counts[item["status"]] = status_counts.get(item["status"], 0) + 1
print(status_counts)
