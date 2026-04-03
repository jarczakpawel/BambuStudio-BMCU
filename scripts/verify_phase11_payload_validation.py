#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
config_h = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.hpp').read_text()
config_cpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.cpp').read_text()
host_cpp = (root / 'tools/pjarczak_bambu_linux_host/LinuxPluginHost.cpp').read_text()
apply_py = (root / 'scripts/apply_pjarczak_linux_bridge.py').read_text()
need = [
    'validate_linux_so_binary',
    'validate_linux_payload_file',
    'not an ELF binary',
    'ELF machine does not match host architecture',
]
for n in need:
    assert n in config_cpp or n in config_h, n
assert ('validate_linux_payload_file(path, &reason)' in host_cpp) or ('validate_linux_so_binary(path, &reason)' in host_cpp)
assert 'validate_linux_payload_file(file_path, &validate_reason)' in apply_py
print({'phase11_validation': True})
