#!/usr/bin/env python3
# build_auto.py - Auto-detect LLVM path, generate llvm_libs.rsp and rebuild_auto.bat, then build.
# Usage: python build_auto.py [--no-run] [--llvm PATH]
#   --no-run   Only generate files, do not run rebuild_auto.bat
#   --llvm     Use this LLVM path instead of auto-detection

import os
import re
import sys
import subprocess
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent
LLVM_LIBS_RSP = SCRIPT_DIR / "llvm_libs.rsp"
REBUILD_ALL_BAT = SCRIPT_DIR / "rebuild_all.bat"
REBUILD_AUTO_BAT = SCRIPT_DIR / "rebuild_auto.bat"


def detect_llvm_path(override: str | None) -> Path | None:
    """Auto-detect LLVM install path on Windows (MSVC layout: .../lib/*.lib, .../include/llvm/)."""
    # 1. Explicit override
    if override:
        p = Path(override).resolve()
        if (p / "lib" / "LLVMCore.lib").exists():
            return p
        if (p / "include" / "llvm").exists():
            return p
        return None

    # 2. Environment
    for env in ("LLVM_PATH", "LLVM_DIR", "LLVM_HOME"):
        v = os.environ.get(env)
        if v:
            p = Path(v).resolve()
            if (p / "lib" / "LLVMCore.lib").exists() or (p / "include" / "llvm").exists():
                return p

    # 3. Common locations
    candidates = [
        Path(r"C:\LLVM-dev"),
        Path(r"C:\Program Files\LLVM"),
        Path(r"C:\llvm"),
    ]
    for base in candidates:
        if not base.exists():
            continue
        if base.is_dir():
            # Direct LLVM root (e.g. C:\LLVM-dev\clang+llvm-21.1.8-...)
            if (base / "lib" / "LLVMCore.lib").exists():
                return base
            # Parent of versioned folder (e.g. C:\LLVM-dev)
            for sub in base.iterdir():
                if sub.is_dir() and (sub / "lib" / "LLVMCore.lib").exists():
                    return sub
        break

    # 4. From clang in PATH (e.g. .../bin/clang.exe -> root = parent of bin)
    try:
        out = subprocess.run(
            ["where", "clang"],
            capture_output=True,
            text=True,
            timeout=5,
            cwd=SCRIPT_DIR,
        )
        if out.returncode == 0 and out.stdout.strip():
            first = out.stdout.strip().splitlines()[0].strip()
            clang_path = Path(first)
            if clang_path.suffix.lower() in (".exe", ""):
                # .../bin/clang.exe -> .../bin -> ...
                root = clang_path.parent.parent
                if (root / "lib" / "LLVMCore.lib").exists():
                    return root
    except Exception:
        pass

    return None


def generate_llvm_libs_rsp(llvm_root: Path) -> None:
    """Generate llvm_libs.rsp with paths under llvm_root. Preserves lib list from existing file or scans lib/."""
    lib_dir = llvm_root / "lib"
    if not lib_dir.exists():
        raise FileNotFoundError(f"LLVM lib directory not found: {lib_dir}")

    # Prefer same list as current llvm_libs.rsp (basenames only)
    lib_basenames = []
    if LLVM_LIBS_RSP.exists():
        for line in LLVM_LIBS_RSP.read_text(encoding="utf-8").splitlines():
            line = line.strip().strip('"')
            if not line or line.startswith("#"):
                continue
            name = Path(line).name
            if name.endswith(".lib"):
                lib_basenames.append(name)
    if not lib_basenames:
        # Fallback: all LLVM*.lib in lib/
        lib_basenames = sorted(p.name for p in lib_dir.glob("LLVM*.lib"))

    lines = [f'"{lib_dir / name}"' for name in lib_basenames if (lib_dir / name).exists()]
    missing = [n for n in lib_basenames if not (lib_dir / n).exists()]
    if missing:
        print(f"Warning: {len(missing)} libs not found under {lib_dir}: {missing[:5]}...")

    LLVM_LIBS_RSP.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"Wrote {LLVM_LIBS_RSP} ({len(lines)} libs)")


def generate_rebuild_auto_bat(llvm_root: Path) -> None:
    """Generate rebuild_auto.bat from rebuild_all.bat with LLVM_DIR set to llvm_root."""
    if not REBUILD_ALL_BAT.exists():
        raise FileNotFoundError(f"Template not found: {REBUILD_ALL_BAT}")

    content = REBUILD_ALL_BAT.read_text(encoding="utf-8")
    # Replace the line that sets LLVM_DIR (e.g. set LLVM_DIR=C:\LLVM-dev\...)
    # Use a lambda so re.sub does not interpret backslashes in the path (e.g. \L) as regex escapes
    new_line = f"set LLVM_DIR={llvm_root}"
    pattern = re.compile(r"^set LLVM_DIR=.*$", re.MULTILINE)
    if not pattern.search(content):
        raise ValueError("Could not find 'set LLVM_DIR=...' in rebuild_all.bat")
    new_content = pattern.sub(lambda m: new_line, content, count=1)
    REBUILD_AUTO_BAT.write_text(new_content, encoding="utf-8")
    print(f"Wrote {REBUILD_AUTO_BAT} with LLVM_DIR={llvm_root}")


def run_build() -> int:
    """Run rebuild_auto.bat in a new process. Returns exit code."""
    print(f"Running {REBUILD_AUTO_BAT} ...")
    return subprocess.call(
        [str(REBUILD_AUTO_BAT)],
        cwd=SCRIPT_DIR,
        shell=True,
    )


def main() -> int:
    args = [a for a in sys.argv[1:] if a and not a.startswith("-")]
    no_run = "--no-run" in sys.argv
    llvm_override = None
    i = 0
    while i < len(sys.argv):
        if sys.argv[i] == "--llvm" and i + 1 < len(sys.argv):
            llvm_override = sys.argv[i + 1]
            i += 2
            continue
        i += 1

    llvm_root = detect_llvm_path(llvm_override)
    if not llvm_root:
        print("Error: Could not detect LLVM path. Install LLVM (e.g. clang+llvm-*-x86_64-pc-windows-msvc) and either:", file=sys.stderr)
        print("  - Set environment variable LLVM_PATH or LLVM_DIR to the LLVM root, or", file=sys.stderr)
        print("  - Run: python build_auto.py --llvm C:\\path\\to\\llvm", file=sys.stderr)
        return 1

    llvm_root = llvm_root.resolve()
    print(f"Using LLVM: {llvm_root}")

    generate_llvm_libs_rsp(llvm_root)
    generate_rebuild_auto_bat(llvm_root)

    if no_run:
        print("Skipping build (--no-run). Run rebuild_auto.bat to build.")
        return 0
    return run_build()


if __name__ == "__main__":
    sys.exit(main())
