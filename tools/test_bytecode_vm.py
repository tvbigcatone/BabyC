#!/usr/bin/env python3
"""Differential smoke test for the opt-in compact bytecode VM."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import babyc_env

COUNTERS = re.compile(r"\[babyc-bytecode-vm-prod\] attempts=(\d+) runs=(\d+)")
INDEX_COPY = re.compile(r"index_copy_fuse=(\d+)")


def get_sources(root):
    return [
        root / "src/tests/546_ir_struct_array_assign.c",
        root / "src/tests/889_vm_struct_array_return_copy.c",
        root / "src/tests/891_vm_local_scalar_array.c",
        root / "src/tests/892_vm_struct_array_copy_no_call.c",
        root / "src/tests/893_vm_struct_return_effect_fallback.c",
        root / "src/tests/895_vm_dynamic_struct_array_index.c",
        root / "src/tests/897_vm_dynamic_struct_array_loop.c",
        root / "src/tests/899_vm_dynamic_struct_array_copy.c",
        root / "src/tests/901_vm_dynamic_struct_array_copy_padding.c",
        root / "src/tests/903_vm_multidim_struct_array.c",
        root / "src/tests/905_vm_multidim_struct_array_copy.c",
        root / "src/tests/913_vm_global_struct_array_fields.c",
        root / "src/tests/924_vm_global_struct_array_copy.c",
        root / "src/tests/925_vm_global_multidim_struct_fields.c",
        root / "src/tests/926_vm_global_multidim_struct_copy.c",
        root / "src/tests/930_vm_bitfield_pack_direct.c",
        root / "src/tests/931_vm_bitfield_comma_direct.c",
        root / "src/tests/495_bitfield_pack.c",
        root / "src/tests/499_bitfield_comma.c",
        root / "src/tests/550_ir_bf_array_trunc.c",
        root / "src/tests/774_bitfield_compound_soft.c",
        root / "src/tests/887_vm_local_multidim_unsigned.c",
        root / "src/tests/799_vm_bytecode_brlt.c",
        root / "src/tests/800_vm_arith_wrap.c",
        root / "src/tests/801_vm_bytecode_scalar.c",
        root / "src/tests/802_vm_indexed_memory.c",
        root / "src/tests/803_vm_global_scalar.c",
        root / "src/tests/804_vm_division.c",
        root / "src/tests/805_vm_operator_matrix.c",
        root / "src/tests/806_vm_global_array.c",
        root / "src/tests/808_vm_terminal_printf.c",
        root / "src/tests/809_vm_global_array_types.c",
        root / "src/tests/810_vm_unsigned_short.c",
        root / "src/tests/811_vm_typed_memory_matrix.c",
        root / "src/tests/812_vm_control_flow.c",
        root / "src/tests/813_vm_global_alias.c",
        root / "src/tests/815_vm_call_fallback_once.c",
        root / "src/tests/816_vm_call_fallback_args.c",
        root / "src/tests/817_vm_terminal_scalar_call.c",
        root / "src/tests/818_vm_nested_pure_scalar_call.c",
        root / "src/tests/819_vm_branchy_call_side_effect.c",
        root / "src/tests/839_vm_ptr_param_offset.c",
        root / "src/tests/845_vm_global_narrow_incdec.c",
        root / "src/tests/847_vm_compact_narrow_incdec.c",
        root / "src/tests/849_vm_compact_loop_bench.c",
        root / "src/tests/864_vm_nested_global_calls.c",
        root / "src/tests/865_vm_recursive_global_effects.c",
        root / "tools/fixtures/830_vm_unsigned_short_array.c",
        root / "tools/fixtures/831_vm_multidim_array.c",
        root / "tools/fixtures/832_vm_pointer_array.c",
        root / "tools/fixtures/833_vm_multidim_3d.c",
        root / "tools/fixtures/834_vm_multidim_4d.c",
    ]


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--babyc", default="src/babyc", help="path to BabyC executable")
    args = ap.parse_args()
    root = Path(__file__).resolve().parents[1]
    binary = Path(args.babyc)
    if not binary.is_absolute():
        binary = (Path.cwd() / binary).resolve()
    sources = get_sources(root)
    only = os.environ.get("BT_ONLY")
    if only == "extras":
        # Run only the supplemental strict-mode gates below. Re-running every
        # corpus source here doubled the runtime and made the aggregate gate
        # needlessly fragile under CI time limits.
        sources = []
    elif only:
        sources = [x for x in sources if x.name == only]
    for source in sources:
        expected = source.with_suffix(".expect").read_text().strip()
        results = {}
        if source.name == "830_vm_unsigned_short_array.c":
            modes = (("tree", {"BABYC_PATH": "tree", "BABYC_EXEC": "tree"}), ("linear", {"BABYC_PATH": "linear", "BABYC_EXEC": "vm"}), ("bytecode", {"BABYC_PATH": "bytecode", "BABYC_EXEC": "vm", "BABYC_BYTECODE_VM": "1", "BABYC_VM_PROFILE": "1"}))
        elif source.name in ("546_ir_struct_array_assign.c", "889_vm_struct_array_return_copy.c",
                             "895_vm_dynamic_struct_array_index.c", "897_vm_dynamic_struct_array_loop.c",
                             "899_vm_dynamic_struct_array_copy.c",
                             "901_vm_dynamic_struct_array_copy_padding.c",
                             "903_vm_multidim_struct_array.c",
                             "905_vm_multidim_struct_array_copy.c",
                             "913_vm_global_struct_array_fields.c",
                             "924_vm_global_struct_array_copy.c",
                             "925_vm_global_multidim_struct_fields.c",
                             "926_vm_global_multidim_struct_copy.c",
                             "495_bitfield_pack.c", "499_bitfield_comma.c",
                             "930_vm_bitfield_pack_direct.c", "931_vm_bitfield_comma_direct.c",
                             "550_ir_bf_array_trunc.c", "774_bitfield_compound_soft.c",
                             "887_vm_local_multidim_unsigned.c", "831_vm_multidim_array.c", "832_vm_pointer_array.c",
                             "833_vm_multidim_3d.c", "834_vm_multidim_4d.c"):
            modes = (("tree", {"BABYC_PATH": "tree", "BABYC_EXEC": "tree"}),
                     ("linear", {"BABYC_PATH": "linear", "BABYC_EXEC": "vm"}),
                     ("bytecode", {"BABYC_PATH": "bytecode", "BABYC_EXEC": "vm", "BABYC_BYTECODE_VM": "1", "BABYC_VM_PROFILE": "1"}))
        else:
            modes = (("linear", {"BABYC_PATH": "linear", "BABYC_EXEC": "vm"}), ("bytecode", {"BABYC_PATH": "bytecode", "BABYC_EXEC": "vm", "BABYC_BYTECODE_VM": "1", "BABYC_VM_PROFILE": "1"}))
        for mode, extra in modes:
            env = os.environ.copy()
            env.pop("BABYC_BYTECODE_VM", None); env.pop("BABYC_EXEC", None)
            env.pop("BABYC_VM_PROFILE", None)
            env.update(extra)
            proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                                  capture_output=True, text=True, timeout=20)
            stdout = proc.stdout.strip()
            if proc.returncode != 0 or stdout != expected:
                print(f"FAIL {source.name} {mode}: rc={proc.returncode}, stdout={stdout!r}, expected={expected!r}", file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
            results[mode] = stdout
            if mode == "bytecode":
                m = COUNTERS.search(proc.stderr)
                attempts, runs = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
                if source.name in ("893_vm_struct_return_effect_fallback.c",
                                   "887_vm_local_multidim_unsigned.c",
                                   "802_vm_indexed_memory.c", "832_vm_pointer_array.c",
                                   "839_vm_ptr_param_offset.c", "865_vm_recursive_global_effects.c"):
                    if runs != 0:
                        print(f"FAIL {source.name}: unsupported case unexpectedly entered compact execution: attempts={attempts}, runs={runs}", file=sys.stderr)
                        print(proc.stderr, file=sys.stderr)
                        return 1
                    print(f"{source.name}: expected compact fallback attempts={attempts}, runs={runs}")
                else:
                    if attempts < 1 or runs < 1:
                        print(f"FAIL {source.name}: bytecode path not exercised: attempts={attempts}, runs={runs}", file=sys.stderr)
                        print(proc.stderr, file=sys.stderr)
                        return 1
                    print(f"{source.name}: bytecode counters attempts={attempts}, runs={runs}")
                if source.name in ("818_vm_nested_pure_scalar_call.c",
                                   "819_vm_branchy_call_side_effect.c",
                                   "864_vm_nested_global_calls.c"):
                    # Both the caller and its scalar callee must enter compact
                    # bytecode. In 843, g must remain 1, proving the branchy
                    # callee's side effect was not replayed by fallback.
                    if attempts < 2 or runs < 2:
                        print(f"FAIL {source.name}: nested compact execution not proven: "
                              f"attempts={attempts}, runs={runs}", file=sys.stderr)
                        print(proc.stderr, file=sys.stderr)
                        return 1
                    print(f"{source.name}: nested compact execution verified "
                          f"(attempts={attempts}, runs={runs})")
                if source.name == "802_vm_indexed_memory.c":
                    fusion = INDEX_COPY.search(proc.stderr)
                    fused = int(fusion.group(1)) if fusion else 0
                    print(f"{source.name}: indexed-copy fusions={fused} (zero means the lowerer did not select this optimization)")
        reference_mode = "tree" if source.name in ("546_ir_struct_array_assign.c", "889_vm_struct_array_return_copy.c", "887_vm_local_multidim_unsigned.c", "830_vm_unsigned_short_array.c", "831_vm_multidim_array.c", "832_vm_pointer_array.c", "833_vm_multidim_3d.c", "834_vm_multidim_4d.c", "895_vm_dynamic_struct_array_index.c", "897_vm_dynamic_struct_array_loop.c") else "linear"
        if any(results[m] != results[reference_mode] for m in results):
            print(f"FAIL {source.name}: execution modes output mismatch", file=sys.stderr)
            return 1
        print(f"PASS {source.name}: execution modes outputs match ({results[reference_mode]})")
        if source.name == "839_vm_ptr_param_offset.c":
            # vm_strict is the no-tree-fallback gate. Keep this focused on
            # pointer parameters indexed by a runtime offset for both int and
            # unsigned-char pointees; compare exact output to the fixture.
            env = os.environ.copy()
            env.update({"BABYC_PATH": "bytecode", "BABYC_STRICT": "1", "BABYC_EXEC": "vm_strict", "BABYC_BYTECODE_VM": "1", "BABYC_VM_STRICT": "1",
                        "BABYC_VM_PROFILE": "1"})
            proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                                  capture_output=True, text=True, timeout=20)
            if proc.returncode != 0 or proc.stdout.strip() != expected:
                print(f"FAIL {source.name} vm_strict: rc={proc.returncode}, "
                      f"stdout={proc.stdout.strip()!r}, expected={expected!r}", file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
            print(f"PASS {source.name}: vm_strict pointer-parameter indexing ({expected})")
        elif source.name in ("830_vm_unsigned_short_array.c",
                             "833_vm_multidim_3d.c", "834_vm_multidim_4d.c"):
            # Promote known-good typed/multidimensional array cases into the
            # no-tree-fallback gate. These cases previously only compared the
            # tree/linear/compact modes, so an accidental strict-only fallback
            # could go unnoticed.
            env = os.environ.copy()
            env.update({"BABYC_PATH": "bytecode", "BABYC_STRICT": "1", "BABYC_EXEC": "vm_strict", "BABYC_BYTECODE_VM": "1", "BABYC_VM_STRICT": "1",
                        "BABYC_VM_PROFILE": "1"})
            proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                                  capture_output=True, text=True, timeout=20)
            if proc.returncode != 0 or proc.stdout.strip() != expected:
                print(f"FAIL {source.name} vm_strict: rc={proc.returncode}, "
                      f"stdout={proc.stdout.strip()!r}, expected={expected!r}", file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
            if "vm_strict: tree fallback" in proc.stderr:
                print(f"FAIL {source.name}: vm_strict unexpectedly reported tree fallback", file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
            print(f"PASS {source.name}: vm_strict typed/multidimensional array ({expected})")
    if only and only != "extras":
        return 0
    # Signed-int 2D globals are admitted only when the strict path really runs;
    # a matching output through tree fallback is not counted as VM coverage.
    source = root / "tools/fixtures/831_vm_multidim_array.c"
    env = os.environ.copy()
    env.update({"BABYC_PATH": "bytecode", "BABYC_STRICT": "1", "BABYC_EXEC": "vm_strict", "BABYC_BYTECODE_VM": "1", "BABYC_VM_STRICT": "1",
                "BABYC_VM_PROFILE": "1"})
    proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                          capture_output=True, text=True, timeout=20)
    if proc.returncode != 0 or proc.stdout.strip() != source.with_suffix(".expect").read_text().strip():
        print(f"FAIL {source.name}: strict execution mismatch, rc={proc.returncode}", file=sys.stderr)
        print(proc.stdout, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 1
    if "vm_strict: tree fallback" in proc.stderr:
        print(f"FAIL {source.name}: vm_strict unexpectedly reported tree fallback", file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 1
    print(f"PASS {source.name}: vm_strict signed-int 2D global array (no tree fallback)")

    # Strict/no-tree-fallback coverage for every currently admitted fixture.
    # Keep the known indexed-memory fusion gap explicit; 831's signed-int 2D
    # global array is now strict-covered after fixing the row-stride heuristic.
    strict_covered = {
        "546_ir_struct_array_assign.c", "889_vm_struct_array_return_copy.c",
        "895_vm_dynamic_struct_array_index.c", "897_vm_dynamic_struct_array_loop.c",
                             "899_vm_dynamic_struct_array_copy.c",
                             "901_vm_dynamic_struct_array_copy_padding.c",
        "903_vm_multidim_struct_array.c",
                             "905_vm_multidim_struct_array_copy.c",
        "913_vm_global_struct_array_fields.c",
        "887_vm_local_multidim_unsigned.c", "799_vm_bytecode_brlt.c", "800_vm_arith_wrap.c",
        "801_vm_bytecode_scalar.c", "803_vm_global_scalar.c",
        "804_vm_division.c", "805_vm_operator_matrix.c",
        "806_vm_global_array.c", "808_vm_terminal_printf.c",
        "809_vm_global_array_types.c", "810_vm_unsigned_short.c",
        "811_vm_typed_memory_matrix.c", "812_vm_control_flow.c",
        "813_vm_global_alias.c", "817_vm_terminal_scalar_call.c",
        "839_vm_ptr_param_offset.c", "830_vm_unsigned_short_array.c",
        "845_vm_global_narrow_incdec.c", "847_vm_compact_narrow_incdec.c",
        "849_vm_compact_loop_bench.c",
        "831_vm_multidim_array.c", "832_vm_pointer_array.c", "833_vm_multidim_3d.c",
        "834_vm_multidim_4d.c", "899_vm_dynamic_struct_array_copy.c",
        "901_vm_dynamic_struct_array_copy_padding.c",
    }
    for source in sources:
        if source.name not in strict_covered or source.name in (
                "839_vm_ptr_param_offset.c", "830_vm_unsigned_short_array.c",
                "831_vm_multidim_array.c", "833_vm_multidim_3d.c", "834_vm_multidim_4d.c"):
            continue  # these already have dedicated strict checks above
        expected = source.with_suffix(".expect").read_text().strip()
        env = os.environ.copy()
        env.update({"BABYC_PATH": "bytecode", "BABYC_STRICT": "1", "BABYC_EXEC": "vm_strict", "BABYC_BYTECODE_VM": "1", "BABYC_VM_STRICT": "1",
                    "BABYC_VM_PROFILE": "1"})
        proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                              capture_output=True, text=True, timeout=20)
        if proc.returncode != 0 or proc.stdout.strip() != expected:
            print(f"FAIL {source.name} vm_strict: rc={proc.returncode}, "
                  f"stdout={proc.stdout.strip()!r}, expected={expected!r}", file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1
        if "vm_strict: tree fallback" in proc.stderr:
            print(f"FAIL {source.name}: vm_strict reported tree fallback", file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1
        print(f"PASS {source.name}: vm_strict ({expected})")

    # Runtime trap path: the compact VM must attempt the body, abort before /0,
    # and preserve the same canonical diagnostic as the established executor.
    source = root / "src/tests/767_lin_divzero_diag.c"
    trap_results = {}
    for mode, bytecode in (("linear", False), ("bytecode", True)):
        env = os.environ.copy()
        env.update({"BABYC_PATH": "linear", "BABYC_EXEC": "vm", "BABYC_VM_PROFILE": "1", "BABYC_COV": "1"})
        env.pop("BABYC_BYTECODE_VM", None); env.pop("BABYC_EXEC", None)
        if bytecode:
            env["BABYC_PATH"] = "bytecode"; env["BABYC_EXEC"] = "vm"; env["BABYC_BYTECODE_VM"] = "1"
        proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                              capture_output=True, text=True, timeout=20)
        diagnostic = [line for line in proc.stdout.splitlines()
                      if "767_lin_divzero_diag.c:" in line or "division by zero" in line
                      or line.strip() == "x = x / z;"]
        trap_results[mode] = (proc.returncode, diagnostic)
        if "division by zero" not in proc.stdout:
            print(f"FAIL {source.name} {mode}: canonical division-by-zero diagnostic missing", file=sys.stderr)
            print(proc.stdout, file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1
        if bytecode:
            m = COUNTERS.search(proc.stderr)
            attempts, runs = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
            if attempts < 1 or runs != 0:
                print(f"FAIL {source.name}: expected VM abort/fallback, got attempts={attempts}, runs={runs}", file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
            print(f"{source.name}: expected VM abort/fallback attempts={attempts}, runs={runs}")
    if trap_results["linear"] != trap_results["bytecode"]:
        print(f"FAIL {source.name}: division-by-zero diagnostic/return mismatch", file=sys.stderr)
        print(trap_results, file=sys.stderr)
        return 1
    print(f"PASS {source.name}: abort/fallback preserves the canonical diagnostic")

    # Bounds-check parity for positive and negative global-array indices.
    for source_name, expected_diag in (
            ("807_vm_global_array_oob.c", "array index 2 out of bounds (size 2)"),
            ("814_vm_global_array_negative_oob.c", "array index -1 out of bounds (size 2)")):
        source = root / "src/tests" / source_name
        oob_results = {}
        for mode, bytecode in (("linear", False), ("bytecode", True)):
            env = os.environ.copy()
            env.update({"BABYC_PATH": "linear", "BABYC_EXEC": "vm", "BABYC_VM_PROFILE": "1", "BABYC_COV": "1", "BABYC_SAFE": "1"})
            env.pop("BABYC_BYTECODE_VM", None); env.pop("BABYC_EXEC", None)
            if bytecode:
                env["BABYC_PATH"] = "bytecode"; env["BABYC_EXEC"] = "vm"; env["BABYC_BYTECODE_VM"] = "1"
            proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                                  capture_output=True, text=True, timeout=20)
            oob_results[mode] = (proc.returncode, proc.stdout)
            if expected_diag not in proc.stdout:
                print(f"FAIL {source.name} {mode}: canonical bounds diagnostic missing", file=sys.stderr)
                print(proc.stdout, file=sys.stderr)
                print(proc.stderr, file=sys.stderr)
                return 1
            if bytecode:
                m = COUNTERS.search(proc.stderr)
                attempts, runs = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
                if attempts < 1 or runs != 0:
                    print(f"FAIL {source.name}: expected bounds abort/fallback, got attempts={attempts}, runs={runs}", file=sys.stderr)
                    print(proc.stderr, file=sys.stderr)
                    return 1
                print(f"{source.name}: expected bounds abort/fallback attempts={attempts}, runs={runs}")
        if oob_results["linear"] != oob_results["bytecode"]:
            print(f"FAIL {source.name}: bounds diagnostic/return mismatch", file=sys.stderr)
            print(oob_results, file=sys.stderr)
            return 1
        print(f"PASS {source.name}: global-array bounds abort preserves canonical diagnostics")

    # Compact-VM safety for every dimension of local multidimensional struct
    # arrays. The legacy linear member path did not reliably diagnose these
    # invalid lvalues, so the compact path must fail before dereferencing and
    # report the offending dimension directly (rather than fall back unsafely).
    for source_name, expected_diag in (
            ("909_vm_multidim_struct_oob_outer.c", "array index 2 out of bounds (size 2)"),
            ("911_vm_multidim_struct_oob_inner.c", "array index 3 out of bounds (size 3)")):
        source = root / "src/tests" / source_name
        env = os.environ.copy()
        env.update({"BABYC_PATH": "bytecode",
                    "BABYC_VM_PROFILE": "1", "BABYC_COV": "1", "BABYC_SAFE": "1"})
        proc = subprocess.run([str(binary), str(source)], cwd=root, env=env,
                              capture_output=True, text=True, timeout=20)
        if proc.returncode == 0 or expected_diag not in proc.stdout:
            print(f"FAIL {source.name}: compact safe-mode bounds diagnostic missing", file=sys.stderr)
            print(f"rc={proc.returncode} stdout={proc.stdout!r} stderr={proc.stderr!r}", file=sys.stderr)
            return 1
        m = COUNTERS.search(proc.stderr)
        attempts, runs = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
        if attempts < 1 or runs != 0:
            print(f"FAIL {source.name}: expected pre-dereference VM abort, got attempts={attempts}, runs={runs}", file=sys.stderr)
            print(proc.stderr, file=sys.stderr)
            return 1
        print(f"PASS {source.name}: dimension-specific safe-mode diagnostic before access")

    # Narrow local ++/-- edge matrix: require both strict and compact execution.
    # Output parity alone is not coverage unless the compact run counter advances.
    source = root / "src/tests/843_vm_narrow_incdec_edges.c"
    expected = source.with_suffix(".expect").read_text().strip()
    strict_env = os.environ.copy()
    strict_env.update({"BABYC_PATH": "bytecode", "BABYC_STRICT": "1", "BABYC_EXEC": "vm_strict", "BABYC_BYTECODE_VM": "1", "BABYC_VM_STRICT": "1",
                       "BABYC_VM_PROFILE": "1"})
    proc = subprocess.run([str(binary), str(source)], cwd=root, env=strict_env,
                          capture_output=True, text=True, timeout=20)
    if proc.returncode != 0 or proc.stdout.strip() != expected:
        print(f"FAIL {source.name} vm_strict: rc={proc.returncode}, stdout={proc.stdout.strip()!r}, expected={expected!r}", file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 1
    if "vm_strict: tree fallback" in proc.stderr:
        print(f"FAIL {source.name}: strict execution reported tree fallback", file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 1
    print(f"PASS {source.name}: strict linear VM narrow ++/-- ({expected})")

    compact_env = os.environ.copy()
    compact_env.update({"BABYC_PATH": "bytecode",
                        "BABYC_VM_PROFILE": "1"})
    proc = subprocess.run([str(binary), str(source)], cwd=root, env=compact_env,
                          capture_output=True, text=True, timeout=20)
    m = COUNTERS.search(proc.stderr)
    attempts, runs = (int(m.group(1)), int(m.group(2))) if m else (0, 0)
    if proc.returncode != 0 or proc.stdout.strip() != expected or attempts < 1 or runs < 1:
        print(f"FAIL {source.name}: expected compact execution, rc={proc.returncode}, attempts={attempts}, runs={runs}", file=sys.stderr)
        print(proc.stdout, file=sys.stderr)
        print(proc.stderr, file=sys.stderr)
        return 1
    print(f"PASS {source.name}: compact bytecode edge matrix (attempts={attempts}, runs={runs})")
    return 0


def run_all(argv):
    """Run every source in its own child process so that one failure does not
    hide the others (W37). Exit code is non-zero if at least one case fails."""
    ap = argparse.ArgumentParser(add_help=False)
    ap.add_argument("--babyc", default="src/babyc")
    ns, _ = ap.parse_known_args(argv)
    root = Path(__file__).resolve().parents[1]
    names = [x.name for x in get_sources(root)] + ["extras"]
    failed = []
    for name in names:
        env = os.environ.copy()
        env["BT_ONLY"] = name
        try:
            proc = subprocess.run([sys.executable, str(Path(__file__).resolve()),
                                   "--babyc", ns.babyc],
                                  env=env, capture_output=True, text=True,
                                  timeout=60)
        except subprocess.TimeoutExpired:
            failed.append(name)
            print(f"TIMEOUT group={name} after 60s", flush=True)
            continue
        sys.stdout.write(proc.stdout)
        sys.stdout.flush()
        if proc.returncode != 0:
            failed.append(name)
            sys.stderr.write(proc.stderr)
    print(f"bytecode-vm: {len(names) - len(failed)}/{len(names)} groups passed"
          + (f"; FAILED: {', '.join(failed)}" if failed else ""))
    return 1 if failed else 0


if __name__ == "__main__":
    if os.environ.get("BT_ONLY"):
        raise SystemExit(main())
    raise SystemExit(run_all(sys.argv[1:]))
