#!/usr/bin/env python3
"""Generate simple C programs for differential testing vs gcc."""
import random, sys

def gen(seed):
    rng = random.Random(seed)
    nvars = rng.randint(2, 5)
    vars = [f"v{i}" for i in range(nvars)]
    lines = ["#include <stdio.h>", "int main(void) {"]
    for v in vars:
        lines.append(f"  int {v} = {rng.randint(0, 20)};")
    for _ in range(rng.randint(5, 15)):
        a, b = rng.choice(vars), rng.choice(vars)
        op = rng.choice(["+", "-", "*"])
        c = rng.choice(vars)
        lines.append(f"  {c} = {a} {op} {b};")
        lines.append(f"  if ({c} < 0) {c} = -{c};")
        lines.append(f"  {c} = {c} % 1000;")
    lv = rng.choice(vars)
    lines.append(f"  {{ int i; for (i = 0; i < 10; i = i + 1) {lv} = {lv} + i; }}")
    lines.append(f"  printf(\"%d\\n\", {rng.choice(vars)});")
    lines.append("  return 0;")
    lines.append("}")
    return "\n".join(lines)

if __name__ == "__main__":
    seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
    print(gen(seed))
