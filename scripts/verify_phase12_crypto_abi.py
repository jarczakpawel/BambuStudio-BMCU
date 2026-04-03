#!/usr/bin/env python3
from pathlib import Path
root = Path(__file__).resolve().parents[1]
config_hpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.hpp').read_text()
config_cpp = (root / 'src/slic3r/Utils/PJarczakLinuxBridge/PJarczakLinuxBridgeConfig.cpp').read_text()
host_cpp = (root / 'tools/pjarczak_bambu_linux_host/LinuxPluginHost.cpp').read_text()
assert 'sha256_file_hex' in config_hpp
assert 'validate_linux_payload_file_against_manifest' in config_hpp
assert 'validate_linux_payload_set_against_manifest' in config_hpp
assert 'expected_network_abi_version' in config_hpp
assert 'abi_version_matches_expected' in config_hpp
assert 'SHA256_' in config_cpp
assert 'linux_payload_manifest.json' in config_cpp
assert 'bambu_network_get_version' in host_cpp
assert 'abi_version_matches_expected' in host_cpp
print('phase12 crypto+abi checks ok')
