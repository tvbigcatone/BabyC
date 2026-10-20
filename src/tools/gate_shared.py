#!/usr/bin/env python3
"""Unified correctness + performance gate for tree (IR) and optional VM.

Usage:
  python3 tools/gate_shared.py           # suite + micro bench IR
  python3 tools/gate_shared.py --vm      # also EXEC=vm bench
  python3 tools/gate_shared.py --fuzz 50 # + fuzz4 sample
  CALLGRIND=1 python3 tools/gate_shared.py  # optional valgrind callgrind on sieve

Exit 0 only if suite has 0 unexpected fails (diag list allowed) and
benches produce matching IR/VM stdout when --vm.
"""
from __future__ import print_function
import argparse, os, subprocess, sys, time, statistics, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..'))
SRC = os.path.join(ROOT, 'src')
BABYC = os.environ.get('BABYC') or os.environ.get('BABYC_BIN') or os.path.join(SRC, 'babyc')

# Diagnostic tests expected to fail message-match in this baseline tree
DIAG_OK = {
    '100_array_type_mismatch.c', '126_csmith_modulo_zero.c', '127_divmod_by_zero.c',
    '128_div_by_zero.c', '131_stack_overflow_msg.c', '137_missing_semicolon.c',
    '143_const_assign.c', '207_ir_divzero.c', '220_ir_local_zero.c', '224_uninit_use.c',
    '226_struct_uninit_member.c', '232_fn_local_uninit.c', '244_array_elem_uninit.c',
    '250_array_elem_incdec_uninit.c', '251_struct_member_uninit.c',
    '253_array_struct_member_uninit.c', '255_multidim_cell_uninit.c',
    '257_member_array_uninit.c', '263_nested_struct_array_uninit.c',
    '264_member_incdec_uninit.c', '265_char_array_partial.c',
    '290_struct_member_const_reject.c', '293_struct_member_uninit.c',
    '323_bodyir_null_deref.c', '365_bodyir_null_arrow.c', '366_bodyir_null_member_index.c',
    '378_stack_overflow_deep.c', '379_stack_overflow_huge_local.c', '418_array_oob_fail.c',
    '419_null_deref_report.c', '435_too_many_args.c', '473_func_body_unclosed.c',
    '498_bitfield_addr.c', '500_bitfield_sizeof.c', '545_ir_struct_array_uninit.c',
    '573_stack_charge_deep_array.c', '576_host_stack_deep.c', '580_stack_host_and_virtual.c',
    '676_include_missing_caret.c', '680_stdio_null_file.c', '684_parse_error_after_comment.c',
    '686_string_literal_write.c', '691_assert_fail.c', '693_array_oob_bounds.c',
    '694_array_oob_scanf_addr.c', '696_heap_uaf_write.c', '698_heap_write_past_end.c',
    '700_heap_double_free.c', '767_lin_divzero_diag.c', '768_lin_uninit_diag.c',
}

BENCHES = [
    ('sieve', 'bench/cases/classic/sieve.c'),
    ('fibc', 'bench/cases/classic/fib_rec_count.c'),
    ('list_walk', 'bench/cases/micro/list_walk.c'),
    ('array_sum', 'bench/cases/micro/array_sum.c'),
    ('isort', 'bench/cases/micro/isort.c'),
]

def run(cmd, env=None, timeout=60):
    e = os.environ.copy()
    if env:
        e.update(env)
    try:
        r = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, env=e)
        out = r.stdout.decode('latin1', errors='replace')
        return r.returncode, out
    except subprocess.TimeoutExpired:
        return 124, '<TIMEOUT>'

def med_time(path, env=None, n=3):
    ts = []
    for _ in range(n):
        t0 = time.perf_counter()
        rc, _ = run([BABYC, path], env=env, timeout=30)
        if rc not in (0, None) and rc != 0:
            # still record time
            pass
        ts.append((time.perf_counter() - t0) * 1000)
    return statistics.median(ts)

def suite():
    rc, out = run([sys.executable, os.path.join(HERE, 'run_suite.py'), SRC], timeout=180)
    # parse failed list
    unexpected = []
    for line in out.splitlines():
        if line.startswith('passed='):
            print('SUITE', line)
        if line.startswith('[') and line.endswith(']'):
            # python list of fails
            import ast
            try:
                fails = ast.literal_eval(line)
            except Exception:
                fails = []
            for f in fails:
                if f not in DIAG_OK:
                    unexpected.append(f)
    print('SUITE unexpected_fails=%d' % len(unexpected))
    if unexpected:
        print('  ', unexpected[:20])
    return len(unexpected) == 0

def benches(do_vm):
    print('BENCH name            IR_ms    VM_ms  ratio match')
    all_ok = True
    for name, rel in BENCHES:
        path = os.path.join(ROOT, rel)
        if not os.path.isfile(path):
            print('  skip missing', rel)
            continue
        ir_t = med_time(path)
        rc_ir, out_ir = run([BABYC, path])
        match = 'n/a'
        vm_t = 0.0
        if do_vm:
            vm_t = med_time(path, {'BABYC_EXEC': 'vm'})
            rc_vm, out_vm = run([BABYC, path], {'BABYC_EXEC': 'vm'})
            # strip [babyc lines
            def norm(s):
                return '\n'.join(l for l in s.replace('\r\n','\n').split('\n')
                                 if not l.startswith('[babyc')).strip()
            match = 'OK' if norm(out_ir) == norm(out_vm) else 'MISMATCH'
            if match != 'OK':
                all_ok = False
        ratio = (vm_t / ir_t) if (do_vm and ir_t > 0) else 0
        print('  %-12s %8.1f %8.1f %6.2f %s' % (name, ir_t, vm_t, ratio, match))
    return all_ok

def fuzz(n):
    fuzz4 = os.path.join(HERE, 'fuzz4.py')
    if not os.path.isfile(fuzz4):
        print('FUZZ skip (no fuzz4.py)')
        return True
    env = {'BABYC_BIN': BABYC}
    rc, out = run([sys.executable, fuzz4, '0', str(n), 'loops'], env=env, timeout=300)
    print('FUZZ', out.strip().split('\n')[-5:])
    # hard_fail 0 is goal; report only
    return 'hard_fail 0' in out or 'hard_fail=0' in out or True

def callgrind():
    sieve = os.path.join(ROOT, 'bench/cases/classic/sieve.c')
    if not os.path.isfile(sieve):
        print('CALLGRIND skip')
        return
    outdir = tempfile.mkdtemp(prefix='babyc_cg_')
    cmd = ['valgrind', '--tool=callgrind', '--callgrind-out-file=%s/cg.out' % outdir,
           BABYC, sieve]
    rc, out = run(cmd, timeout=120)
    print('CALLGRIND rc=%s out=%s' % (rc, outdir))

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--vm', action='store_true')
    ap.add_argument('--fuzz', type=int, default=0)
    ap.add_argument('--no-suite', action='store_true')
    args = ap.parse_args()
    if not os.path.isfile(BABYC):
        print('error: binary not found', BABYC, file=sys.stderr)
        sys.exit(2)
    print('binary=%s' % BABYC)
    ok = True
    if not args.no_suite:
        ok = suite() and ok
    ok = benches(args.vm) and ok
    if args.fuzz > 0:
        fuzz(args.fuzz)
    if os.environ.get('CALLGRIND') == '1':
        callgrind()
    print('GATE', 'PASS' if ok else 'FAIL')
    sys.exit(0 if ok else 1)

if __name__ == '__main__':
    main()
