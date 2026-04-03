import json
import shutil
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "generated_overlay"

if OUT.exists():
    shutil.rmtree(OUT)

(OUT / "shared").mkdir(parents=True, exist_ok=True)
(OUT / "docs").mkdir(parents=True, exist_ok=True)
(OUT / "adapters").mkdir(parents=True, exist_ok=True)

shutil.copytree(ROOT / "shared", OUT / "shared", dirs_exist_ok=True)
shutil.copytree(ROOT / "docs", OUT / "docs", dirs_exist_ok=True)
shutil.copytree(ROOT / "adapters", OUT / "adapters", dirs_exist_ok=True)

manifest = json.loads((ROOT / "shared" / "pjarczak_linux_plugin_bridge_core" / "method_manifest.json").read_text(encoding="utf-8"))
summary = {
    "total": len(manifest),
    "implemented": sum(1 for m in manifest if m["status"] == "implemented"),
    "partial": sum(1 for m in manifest if m["status"] == "partial"),
    "todo": sum(1 for m in manifest if m["status"] == "todo"),
}
(OUT / "bridge_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
print("generated:", OUT)
