#!/usr/bin/env python3
from pathlib import Path
import shutil
import sys


def copy_tree(src: Path, dst: Path) -> None:
    for item in src.rglob('*'):
        rel = item.relative_to(src)
        out = dst / rel
        if item.is_dir():
            out.mkdir(parents=True, exist_ok=True)
        else:
            out.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(item, out)


def main() -> int:
    if len(sys.argv) != 3:
        print('usage: render_pjarczak_linux_bridge_overlay.py <package_root> <repo_root>')
        return 1

    package_root = Path(sys.argv[1]).resolve()
    repo_root = Path(sys.argv[2]).resolve()
    if not package_root.exists() or not repo_root.exists():
        print('missing path')
        return 2

    for rel in ['src', 'tools', 'docs']:
        src = package_root / rel
        if src.exists():
            copy_tree(src, repo_root / rel)

    patcher = package_root / 'scripts' / 'apply_pjarczak_linux_bridge.py'
    ns = {'__name__': '__main__'}
    sys.argv = [str(patcher), str(repo_root)]
    code = compile(patcher.read_text(encoding='utf-8'), str(patcher), 'exec')
    exec(code, ns)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
