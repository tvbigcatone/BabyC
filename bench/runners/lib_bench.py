#!/usr/bin/env python3
"""Shared bench engine: 6 uniform columns for every case.

Columns (fixed order):
  ats_no_accel  — Body-IR tree, kernel+shape OFF
  ats_accel     — Body-IR tree, kernel+shape ON  (product default)
  vm_no_accel   — BABYC_EXEC=vm, kernel+shape OFF
  vm_accel      — BABYC_EXEC=vm, kernel+shape ON
  python        — CPython reference (if .py exists)
  gcc           — native -O2 (compile on demand or refs/)
"""
from __future__ import annotations

import json
import os
import re
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Dict, List, Optional, Tuple

# Optional YAML; fall back to a minimal parser if PyYAML missing
try:
    import yaml  # type: ignore
except ImportError:
    yaml = None

NOISE = re.compile(
    r"^\[(babyc|babyc-cov|babyc-vm|babyc-ir|hy|fill-hy|call|fp|babyc-bytecode)\]"
)

COL_ORDER = [
    "ats_no_accel",
    "ats_accel",
    "vm_no_accel",
    "vm_accel",
    "python",
    "gcc",
]

COL_LABEL = {
    "ats_no_accel": "ATS_no",
    "ats_accel": "ATS_acc",
    "vm_no_accel": "VM_no",
    "vm_accel": "VM_acc",
    "python": "python",
    "gcc": "gcc",
}


def bench_root() -> Path:
    return Path(__file__).resolve().parents[1]


def project_root() -> Path:
    return bench_root().parent


def find_babyc(explicit: Optional[str] = None) -> Path:
    if explicit:
        p = Path(explicit)
        if p.is_file():
            return p.resolve()
    env = os.environ.get("BABYC")
    if env and Path(env).is_file():
        return Path(env).resolve()
    cand = project_root() / "src" / "babyc"
    if cand.is_file():
        return cand.resolve()
    cand_exe = project_root() / "src" / "babyc.exe"
    if cand_exe.is_file():
        return cand_exe.resolve()
    raise FileNotFoundError("babyc binary not found (set BABYC= or build src/)")


def _strip_noise(text: str) -> str:
    lines = []
    for line in text.replace("\r", "").split("\n"):
        if NOISE.match(line):
            continue
        lines.append(line)
    return "\n".join(lines).strip()


def load_manifest(path: Optional[Path] = None) -> Dict[str, Any]:
    path = path or (bench_root() / "MANIFEST.yml")
    raw = path.read_text(encoding="utf-8")
    if yaml is not None:
        return yaml.safe_load(raw)
    # Minimal fallback: enough for our simple structure
    return _parse_manifest_fallback(raw)


def _parse_manifest_fallback(raw: str) -> Dict[str, Any]:
    """Tiny YAML subset parser for cases list (no external dep)."""
    cases: List[Dict[str, Any]] = []
    cur: Optional[Dict[str, Any]] = None
    defaults: Dict[str, Any] = {"runs": 5, "timeout": 120}
    in_cases = False
    for line in raw.splitlines():
        s = line.strip()
        if not s or s.startswith("#"):
            continue
        if s.startswith("version:"):
            continue
        if s.startswith("defaults:"):
            in_cases = False
            continue
        if s.startswith("runs:"):
            defaults["runs"] = int(s.split(":", 1)[1].strip())
            continue
        if s.startswith("timeout:"):
            defaults["timeout"] = int(s.split(":", 1)[1].strip())
            continue
        if s.startswith("cases:"):
            in_cases = True
            continue
        if not in_cases:
            continue
        if s.startswith("- id:"):
            if cur:
                cases.append(cur)
            cur = {"id": s.split(":", 1)[1].strip(), "tags": [], "gates": []}
            continue
        if cur is None:
            continue
        if s.startswith("path:"):
            cur["path"] = s.split(":", 1)[1].strip()
        elif s.startswith("tags:"):
            inner = s.split(":", 1)[1].strip()
            cur["tags"] = [t.strip() for t in inner.strip("[]").split(",") if t.strip()]
        elif s.startswith("gates:"):
            inner = s.split(":", 1)[1].strip()
            cur["gates"] = [t.strip() for t in inner.strip("[]").split(",") if t.strip()]
    if cur:
        cases.append(cur)
    return {"version": 1, "defaults": defaults, "cases": cases}


def filter_cases(
    cases: List[Dict[str, Any]],
    set_name: str = "all",
    gate: str = "all",
    only: Optional[List[str]] = None,
) -> List[Dict[str, Any]]:
    out = []
    only_set = set(only) if only else None
    for c in cases:
        cid = c["id"]
        tags = set(c.get("tags") or [])
        gates = set(c.get("gates") or [])
        if only_set is not None:
            if cid not in only_set and cid.split(".", 1)[-1] not in only_set:
                continue
        if set_name != "all":
            # set = first tag namespace or explicit tag
            prefix = cid.split(".", 1)[0]
            if set_name != prefix and set_name not in tags:
                continue
        if gate != "all" and gate not in gates:
            continue
        out.append(c)
    return out


def _base_env() -> Dict[str, str]:
    e = os.environ.copy()
    e["BABYC_NO_MEMO"] = "1"
    # Drop leftover exec flags so each mode starts clean
    e.pop("BABYC_EXEC", None)
    e.pop("BABYC_NO_KERNEL", None)
    e.pop("BABYC_NO_SHAPE_FP", None)
    e.pop("BABYC_SHAPE_FP", None)
    return e


def mode_env(mode: str) -> Dict[str, str]:
    e = _base_env()
    if mode == "ats_no_accel":
        e["BABYC_NO_KERNEL"] = "1"
        e["BABYC_NO_SHAPE_FP"] = "1"
    elif mode == "ats_accel":
        pass  # product default: kernel+shape ON
    elif mode == "vm_no_accel":
        e["BABYC_EXEC"] = "vm"
        e["BABYC_NO_KERNEL"] = "1"
        e["BABYC_NO_SHAPE_FP"] = "1"
    elif mode == "vm_accel":
        e["BABYC_EXEC"] = "vm"
    else:
        raise ValueError(f"unknown mode {mode}")
    return e


def run_once(
    cmd: List[str],
    env: Optional[Dict[str, str]] = None,
    timeout: float = 120,
    cwd: Optional[Path] = None,
) -> Tuple[int, str, float]:
    t0 = time.perf_counter()
    try:
        r = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            env=env,
            timeout=timeout,
            cwd=str(cwd) if cwd else None,
        )
        dt = time.perf_counter() - t0
        out = _strip_noise(r.stdout or "")
        return r.returncode, out, dt
    except subprocess.TimeoutExpired:
        return -1, "TIMEOUT", time.perf_counter() - t0


def median_run(
    cmd: List[str],
    env: Optional[Dict[str, str]],
    runs: int,
    timeout: float,
    cwd: Optional[Path] = None,
) -> Tuple[Optional[float], str, int]:
    """Return (median_time|None, output, last_rc)."""
    times: List[float] = []
    out = ""
    rc = 0
    # warm-up (not timed into median)
    run_once(cmd, env, timeout, cwd)
    for _ in range(runs):
        rc, out, dt = run_once(cmd, env, timeout, cwd)
        if rc != 0 or out == "TIMEOUT":
            return None, out, rc
        times.append(dt)
    return statistics.median(times), out, rc


def compile_gcc(src: Path, out_bin: Path, timeout: float = 60) -> bool:
    if out_bin.is_file() and out_bin.stat().st_mtime >= src.stat().st_mtime:
        return True
    out_bin.parent.mkdir(parents=True, exist_ok=True)
    cmd = ["gcc", "-O2", "-o", str(out_bin), str(src), "-lm"]
    r = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    if r.returncode == 0 and out_bin.is_file():
        try:
            out_bin.chmod(out_bin.stat().st_mode | 0o111)
        except OSError:
            pass
        return True
    return False


def measure_case(
    case: Dict[str, Any],
    babyc: Path,
    runs: int = 5,
    timeout: float = 120,
    skip_gcc: bool = False,
    skip_python: bool = False,
) -> Dict[str, Any]:
    root = bench_root()
    src = root / case["path"]
    if not src.is_file():
        return {
            "id": case["id"],
            "status": "MISSING",
            "times": {k: None for k in COL_ORDER},
            "output": "",
            "error": f"source not found: {src}",
        }

    py_src = src.with_suffix(".py")
    result: Dict[str, Any] = {
        "id": case["id"],
        "path": case["path"],
        "times": {},
        "outputs": {},
        "status": "OK",
        "error": "",
    }

    # Reference output: prefer gcc, else first successful ATS
    ref_out: Optional[str] = None

    # --- gcc ---
    if not skip_gcc:
        refs = root / "refs"
        refs.mkdir(parents=True, exist_ok=True)
        # Exact id match only (e.g. micro_isort_gcc). Never reuse another
        # series' binary (stress_fib_multiarg must not match micro.fib_multiarg).
        ref_bin = refs / f"{case['id'].replace('.', '_')}_gcc"
        if not ref_bin.is_file():
            compile_gcc(src, ref_bin, timeout)
        if ref_bin.is_file():
            # ensure executable (copied refs may lack +x)
            try:
                ref_bin.chmod(ref_bin.stat().st_mode | 0o111)
            except OSError:
                pass
            t, out, rc = median_run([str(ref_bin)], _base_env(), runs, timeout)
            result["times"]["gcc"] = t
            result["outputs"]["gcc"] = out
            if rc == 0 and out != "TIMEOUT":
                ref_out = out
            else:
                result["times"]["gcc"] = None
        else:
            result["times"]["gcc"] = None
    else:
        result["times"]["gcc"] = None

    # --- python ---
    if not skip_python and py_src.is_file():
        t, out, rc = median_run(
            [sys.executable, str(py_src)], _base_env(), runs, timeout, cwd=src.parent
        )
        result["times"]["python"] = t if rc == 0 and out != "TIMEOUT" else None
        result["outputs"]["python"] = out
        if ref_out is None and rc == 0 and out != "TIMEOUT":
            ref_out = out
    else:
        result["times"]["python"] = None

    # --- ATS / VM modes ---
    for mode in ("ats_no_accel", "ats_accel", "vm_no_accel", "vm_accel"):
        env = mode_env(mode)
        t, out, rc = median_run([str(babyc), str(src)], env, runs, timeout)
        if rc != 0 or out == "TIMEOUT":
            result["times"][mode] = None
            result["outputs"][mode] = out
            if result["status"] == "OK":
                result["status"] = "FAIL"
                result["error"] = f"{mode}: rc={rc} out={out[:80]}"
            continue
        result["times"][mode] = t
        result["outputs"][mode] = out
        if ref_out is None:
            ref_out = out
        elif out != ref_out:
            result["status"] = "DIFF"
            result["error"] = f"{mode} output mismatch vs ref"
            # keep timing but mark DIFF

    result["output"] = ref_out or ""
    return result


def format_table(rows: List[Dict[str, Any]]) -> str:
    headers = ["case"] + [COL_LABEL[c] for c in COL_ORDER] + ["status"]
    lines = []
    # header
    colw = {
        "case": max(16, max((len(r["id"]) for r in rows), default=16)),
    }
    for c in COL_ORDER:
        colw[c] = 8
    colw["status"] = 6

    def fmt_time(v: Optional[float]) -> str:
        if v is None:
            return "—"
        if v < 0.001:
            return f"{v*1000:.2f}ms"
        return f"{v:.3f}"

    hdr = f"{'case':{colw['case']}}"
    for c in COL_ORDER:
        hdr += f"  {COL_LABEL[c]:>8}"
    hdr += f"  {'status':>6}"
    lines.append(hdr)
    lines.append("-" * len(hdr))

    ok = fail = 0
    for r in rows:
        line = f"{r['id']:{colw['case']}}"
        for c in COL_ORDER:
            line += f"  {fmt_time(r['times'].get(c)):>8}"
        st = r.get("status", "?")
        line += f"  {st:>6}"
        lines.append(line)
        if st == "OK":
            ok += 1
        else:
            fail += 1

    lines.append("-" * len(hdr))
    lines.append(f"OK={ok}  FAIL/DIFF/MISSING={fail}  total={len(rows)}")
    return "\n".join(lines)


def write_results(rows: List[Dict[str, Any]], out_dir: Path, tag: str = "latest") -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    # JSON
    payload = {
        "columns": COL_ORDER,
        "cases": [
            {
                "id": r["id"],
                "path": r.get("path"),
                "times": r.get("times"),
                "status": r.get("status"),
                "output": r.get("output"),
                "error": r.get("error", ""),
            }
            for r in rows
        ],
    }
    (out_dir / f"{tag}.json").write_text(
        json.dumps(payload, indent=2), encoding="utf-8"
    )
    # Markdown table
    md = ["# Bench results", "", "```", format_table(rows), "```", ""]
    (out_dir / f"{tag}.md").write_text("\n".join(md), encoding="utf-8")
