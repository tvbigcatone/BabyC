#!/usr/bin/env python3
"""Generate C programs stressing bitfields, struct locals, compound-assign,
pointer+member, arrays of BF structs — areas touched in S2 bitfield work.

UB-light: every BF member written before read; widths 1..12; signed ranges
respected on init; no div/mod by zero; no jumps/goto; no float/longlong.

usage: gen_bitfield_storm.py SEED > prog.c
"""
import random
import sys

def gen(seed: int) -> str:
    r = random.Random(seed)
    lines = ["#include <stdio.h>"]

    # --- struct templates ---
    n_structs = r.randint(1, 3)
    structs = []
    for si in range(n_structs):
        name = f"S{si}"
        members = []
        # mix of plain + bitfields
        nmem = r.randint(2, 5)
        used_bits = 0
        for mi in range(nmem):
            mname = f"m{mi}"
            kind = r.choice(["plain", "bf_s", "bf_u", "bf_s", "bf_u"])
            if kind == "plain":
                members.append((mname, "int", 0, False))
            else:
                w = r.randint(1, 12)
                signed = kind == "bf_s"
                members.append((mname, "int" if signed else "unsigned", w, signed))
                used_bits += w
        structs.append((name, members))
        lines.append(f"struct {name} {{")
        for mname, base, w, signed in members:
            if w == 0:
                lines.append(f"  {base} {mname};")
            else:
                lines.append(f"  {base} {mname}:{w};")
        lines.append("};")

    # optional helper that takes struct by value / pointer
    if r.random() < 0.7:
        sn, mems = structs[0]
        # pick a bitfield or plain member
        mname, base, w, signed = r.choice(mems)
        lines.append(f"int get_{sn}_{mname}(struct {sn} s) {{ return (int)s.{mname}; }}")
        lines.append(f"void set_{sn}_{mname}(struct {sn} *p, int v) {{ p->{mname} = v; }}")

    lines.append("int main(void) {")
    lines.append("  int t = 0;")
    lines.append("  int i;")

    # locals: one or more struct values
    for si, (sn, mems) in enumerate(structs):
        lines.append(f"  struct {sn} s{si};")
        # always init every member
        for mname, base, w, signed in mems:
            if w == 0:
                val = r.randint(-50, 50)
            elif signed:
                # stay in signed range for width w: -2^(w-1) .. 2^(w-1)-1
                lo = -(1 << (w - 1))
                hi = (1 << (w - 1)) - 1
                val = r.randint(lo, hi)
            else:
                val = r.randint(0, (1 << w) - 1)
            lines.append(f"  s{si}.{mname} = {val};")

    # optional array of first struct
    use_arr = r.random() < 0.6
    if use_arr:
        sn, mems = structs[0]
        asize = r.randint(3, 6)
        lines.append(f"  struct {sn} arr[{asize}];")
        lines.append(f"  for (i = 0; i < {asize}; i++) {{")
        for mname, base, w, signed in mems:
            if w == 0:
                lines.append(f"    arr[i].{mname} = i * 3 + 1;")
            elif signed:
                # keep values small relative to width
                mask = (1 << max(1, w - 1)) - 1
                lines.append(f"    arr[i].{mname} = (i & {mask});")
            else:
                mask = (1 << w) - 1
                lines.append(f"    arr[i].{mname} = (unsigned)(i & {mask});")
        lines.append("  }")

    # ops: compound assign, ++/--, pointer walk, calls
    ops = r.randint(8, 18)
    for _ in range(ops):
        k = r.random()
        si = r.randrange(len(structs))
        sn, mems = structs[si]
        mname, base, w, signed = r.choice(mems)

        if k < 0.25:
            # compound assign on local
            op = r.choice(["+=", "-=", "*=", "&=", "|=", "^="])
            rhs = r.randint(1, 5) if op == "*=" else r.randint(0, 7)
            lines.append(f"  s{si}.{mname} {op} {rhs};")
        elif k < 0.40:
            # prefix/postfix on local BF
            if r.random() < 0.5:
                lines.append(f"  ++s{si}.{mname};")
            else:
                lines.append(f"  s{si}.{mname}++;")
        elif k < 0.55 and use_arr and si == 0:
            # indexed compound
            op = r.choice(["+=", "-=", "&="])
            idx = r.randint(0, asize - 1)
            rhs = r.randint(0, 5)
            lines.append(f"  arr[{idx}].{mname} {op} {rhs};")
        elif k < 0.70 and use_arr and si == 0:
            # pointer walk
            lines.append(f"  {{ struct {sn} *p = arr;")
            lines.append(f"    int j;")
            lines.append(f"    for (j = 0; j < {asize}; j++) {{")
            lines.append(f"      t += (int)p[j].{mname};")
            lines.append(f"      p[j].{mname} += 1;")
            lines.append(f"    }}")
            lines.append(f"  }}")
        elif k < 0.85 and any(True for _ in [1]):
            # call helper if exists
            if f"get_{sn}_{mname}" in "\n".join(lines):
                lines.append(f"  t += get_{sn}_{mname}(s{si});")
                lines.append(f"  set_{sn}_{mname}(&s{si}, {r.randint(0, 7)});")
            else:
                lines.append(f"  t += (int)s{si}.{mname};")
        else:
            # plain accumulate
            lines.append(f"  t += (int)s{si}.{mname};")

    # final checksum over all members
    for si, (sn, mems) in enumerate(structs):
        for mname, base, w, signed in mems:
            lines.append(f"  t += (int)s{si}.{mname};")
    if use_arr:
        lines.append(f"  for (i = 0; i < {asize}; i++) {{")
        for mname, base, w, signed in structs[0][1]:
            lines.append(f"    t += (int)arr[i].{mname};")
        lines.append("  }")

    lines.append('  printf("%d\\n", t);')
    lines.append("  return 0;")
    lines.append("}")
    return "\n".join(lines)

if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    print(gen(seed))
