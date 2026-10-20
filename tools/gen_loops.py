#!/usr/bin/env python3
"""Generate UB-light C programs that stress loop/array/if execution.

Used for differential testing:  gcc (oracle)  vs  BabyC (Irb on)  vs  BabyC (BABYC_NO_IRB=1).
Every array element is written before it is read; divisors are forced non-zero;
shifts only act on masked non-negative values.  Signed overflow is possible, so
compile the oracle with -fwrapv.

usage: gen_loops.py SEED > prog.c
"""
import random
import sys


class Gen:
    def __init__(self, seed):
        self.r = random.Random(seed)
        self.scalars = []      # (name, ctype)
        self.arrays = []       # (name, ctype, size)
        self.ptrs = []         # (name, target array name, ctype)
        self.lines = []
        self.depth = 0

    # ---------- expressions ----------
    def leaf(self, idxvars):
        r = self.r
        k = r.random()
        if k < 0.25:
            return str(r.randint(0, 40))
        if k < 0.55 or not (self.arrays or self.ptrs):
            return r.choice(self.scalars)[0]
        if k < 0.75 and idxvars:
            return r.choice(idxvars)
        if self.ptrs and r.random() < 0.4:
            p, tgt, _ = r.choice(self.ptrs)
            size = next(s for (n, t, s) in self.arrays if n == tgt)
            return "%s[%s]" % (p, self.index(idxvars, size))
        a, _, size = r.choice(self.arrays)
        return "%s[%s]" % (a, self.index(idxvars, size))

    def index(self, idxvars, size):
        r = self.r
        if idxvars and r.random() < 0.7:
            v = r.choice(idxvars)
            if r.random() < 0.3:
                return "(%s + %d) %% %d" % (v, r.randint(0, 5), size)
            return "%s %% %d" % (v, size) if r.random() < 0.5 else v_mod(v, size)
        return str(r.randint(0, size - 1))

    def expr(self, idxvars, depth=0):
        r = self.r
        if depth >= 3 or r.random() < 0.3:
            return self.leaf(idxvars)
        k = r.random()
        a = self.expr(idxvars, depth + 1)
        b = self.expr(idxvars, depth + 1)
        if k < 0.30:
            return "(%s %s %s)" % (a, r.choice(["+", "-", "*"]), b)
        if k < 0.45:
            return "(%s %s %s)" % (a, r.choice(["&", "|", "^"]), b)
        if k < 0.60:
            return "(%s %s %s)" % (a, r.choice(["<", ">", "<=", ">=", "==", "!="]), b)
        if k < 0.68:
            return "(%s %s %s)" % (a, r.choice(["&&", "||"]), b)
        if k < 0.78:
            return "(%s %s ((%s & 7) + 1))" % (a, r.choice(["/", "%"]), b)
        if k < 0.84:
            return "((%s & 4095) %s (%s & 3))" % (a, r.choice(["<<", ">>"]), b)
        if k < 0.92:
            return "(%s%s)" % (r.choice(["-", "!", "~"]), a)
        return "(%s)" % a

    def lvalue(self, idxvars):
        r = self.r
        if self.arrays and r.random() < 0.5:
            if self.ptrs and r.random() < 0.3:
                p, tgt, _ = r.choice(self.ptrs)
                size = next(s for (n, t, s) in self.arrays if n == tgt)
                return "%s[%s]" % (p, self.index(idxvars, size))
            a, _, size = r.choice(self.arrays)
            return "%s[%s]" % (a, self.index(idxvars, size))
        return r.choice(self.scalars)[0]

    # ---------- statements ----------
    def stmt(self, idxvars, loopdepth, inloop):
        r = self.r
        k = r.random()
        pad = "    " * (self.depth + 1)
        if k < 0.42:
            lv = self.lvalue(idxvars)
            op = r.choice(["=", "=", "+=", "-=", "^=", "|=", "&=", "*="])
            e = self.expr(idxvars)
            if op == "*=":
                self.lines.append("%s%s = (%s & 255) * ((%s) & 255);" % (pad, lv, lv, e))
            elif op == "=":
                self.lines.append("%s%s = %s;" % (pad, lv, e))
            else:
                self.lines.append("%s%s %s (%s) & 4095;" % (pad, lv, op, e))
        elif k < 0.50:
            lv = self.lvalue(idxvars)
            self.lines.append("%s%s%s;" % (pad, lv, r.choice(["++", "--"])))
        elif k < 0.62:
            self.lines.append("%sif (%s) {" % (pad, self.expr(idxvars)))
            self.depth += 1
            for _ in range(r.randint(1, 2)):
                self.stmt(idxvars, loopdepth, inloop)
            self.depth -= 1
            if r.random() < 0.5:
                self.lines.append("%s} else {" % pad)
                self.depth += 1
                for _ in range(r.randint(1, 2)):
                    self.stmt(idxvars, loopdepth, inloop)
                self.depth -= 1
            self.lines.append("%s}" % pad)
        elif k < 0.66 and inloop:
            self.lines.append("%sif (%s) %s;" % (pad, self.expr(idxvars), r.choice(["break", "continue"])))
        elif k < 0.90 and loopdepth < 3:
            self.loop(idxvars, loopdepth)
        else:
            lv = self.lvalue(idxvars)
            self.lines.append("%s%s = %s;" % (pad, lv, self.expr(idxvars)))

    def loop(self, idxvars, loopdepth):
        r = self.r
        pad = "    " * (self.depth + 1)
        iv = "i%d_%d" % (loopdepth, r.randint(0, 999))
        self.lines.append("%s{ int %s;" % (pad, iv))
        n = r.randint(2, 14)
        kind = r.choice(["for", "for", "while", "do"])
        nv = idxvars + [iv]
        self.depth += 1
        pad2 = "    " * (self.depth + 1)
        if kind == "for":
            step = r.choice(["%s++", "%s += 2", "%s = %s + 1"])
            st = step % ((iv,) if step.count("%s") == 1 else (iv, iv))
            self.lines.append("%sfor (%s = 0; %s < %d; %s) {" % (pad2, iv, iv, n, st))
            self.depth += 1
            for _ in range(r.randint(1, 4)):
                self.stmt(nv, loopdepth + 1, True)
            self.depth -= 1
            self.lines.append("%s}" % pad2)
        elif kind == "while":
            self.lines.append("%s%s = 0;" % (pad2, iv))
            self.lines.append("%swhile (%s < %d) {" % (pad2, iv, n))
            self.depth += 1
            pad3 = "    " * (self.depth + 1)
            self.lines.append("%s%s++;" % (pad3, iv))
            for _ in range(r.randint(1, 4)):
                self.stmt(nv, loopdepth + 1, True)
            self.depth -= 1
            self.lines.append("%s}" % pad2)
        else:
            self.lines.append("%s%s = 0;" % (pad2, iv))
            self.lines.append("%sdo {" % pad2)
            self.depth += 1
            pad3 = "    " * (self.depth + 1)
            self.lines.append("%s%s++;" % (pad3, iv))
            for _ in range(r.randint(1, 4)):
                self.stmt(nv, loopdepth + 1, True)
            self.depth -= 1
            self.lines.append("%s} while (%s < %d);" % (pad2, iv, n))
        self.depth -= 1
        self.lines.append("%s}" % pad)

    def program(self):
        r = self.r
        types = ["int", "int", "int", "long", "char", "short"]
        ns = r.randint(3, 6)
        for i in range(ns):
            self.scalars.append(("s%d" % i, r.choice(types)))
        na = r.randint(1, 3)
        for i in range(na):
            self.arrays.append(("a%d" % i, r.choice(types), r.randint(4, 24)))
        out = ["#include <stdio.h>", "int main(void)", "{"]
        for n, t in self.scalars:
            out.append("    %s %s = %d;" % (t, n, r.randint(0, 20)))
        for n, t, sz in self.arrays:
            out.append("    %s %s[%d];" % (t, n, sz))
        out.append("    int ix;")
        for n, t, sz in self.arrays:
            out.append("    for (ix = 0; ix < %d; ix++) %s[ix] = ix * %d + %d;" % (sz, n, r.randint(1, 5), r.randint(0, 9)))
        # pointer aliases (only to int arrays: Irb binds int-family element types)
        for n, t, sz in self.arrays:
            if r.random() < 0.5:
                pn = "p_%s" % n
                out.append("    %s *%s = %s;" % (t, pn, n))
                self.ptrs.append((pn, n, t))
        for _ in range(r.randint(2, 6)):
            self.stmt([], 0, False)
        body = self.lines
        out += body
        for n, t in self.scalars:
            out.append('    printf("%s=%%ld\\n", (long)%s);' % (n, n))
        for n, t, sz in self.arrays:
            out.append("    { long cs = 0; for (ix = 0; ix < %d; ix++) cs = cs * 31 + %s[ix]; printf(\"%s:%%ld\\n\", cs); }" % (sz, n, n))
        out += ["    return 0;", "}"]
        return "\n".join(out) + "\n"


def v_mod(v, size):
    return "%s %% %d" % (v, size)


if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    sys.stdout.write(Gen(seed).program())
