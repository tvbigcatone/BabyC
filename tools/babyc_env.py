# -*- coding: utf-8 -*-
"""BabyC execution env helpers — PATH / ACCEL / STRICT axes.

Preferred:
  BABYC_PATH=token|tree|linear|bytecode
  BABYC_ACCEL=0|1
  BABYC_STRICT=0|1

Legacy aliases still work in the binary; tools should set the preferred vars
and clear conflicting legacy keys so parent env cannot override.
"""
from __future__ import print_function
import os

LEGACY_KEYS = (
    "BABYC_EXEC",
    "BABYC_VM_STRICT",
    "BABYC_NO_IR",
    "BABYC_BYTECODE_VM",
    "BABYC_NO_SHAPE_FP",
    "BABYC_NO_KERNEL",
    "BABYC_LOWER",
)


def scrub_legacy(env):
    """Remove legacy axis keys so preferred vars win cleanly."""
    for k in LEGACY_KEYS:
        env.pop(k, None)
    return env


def make_env(path="tree", accel=1, strict=0, base=None, **extra):
    """Build env dict for a BabyC run.

    path: token | tree | linear | bytecode
    accel: 0/1 (master shape+kernel)
    strict: 0/1 (no silent tree residual)
    """
    env = dict(base if base is not None else os.environ)
    scrub_legacy(env)
    env["BABYC_PATH"] = path
    env["BABYC_ACCEL"] = "1" if accel else "0"
    env["BABYC_STRICT"] = "1" if strict else "0"
    # Dual-write legacy aliases so any residual C getenv still sees intent.
    if path == "token":
        env["BABYC_NO_IR"] = "1"
    elif path == "tree":
        env["BABYC_EXEC"] = "tree"
    elif path == "linear":
        env["BABYC_EXEC"] = "vm_strict" if strict else "vm"
    elif path == "bytecode":
        env["BABYC_EXEC"] = "vm_strict" if strict else "vm"
        env["BABYC_BYTECODE_VM"] = "1"
    if strict:
        env["BABYC_VM_STRICT"] = "1"
    if not accel:
        env["BABYC_NO_SHAPE_FP"] = "1"
        env["BABYC_NO_KERNEL"] = "1"
    # Diagnostics / optional overlays keep their own names
    for k, v in extra.items():
        if v is None:
            env.pop(k, None)
        else:
            env[k] = str(v)
    return env


def tree(accel=1, **extra):
    return make_env("tree", accel=accel, strict=0, **extra)


def linear(accel=1, strict=0, **extra):
    return make_env("linear", accel=accel, strict=strict, **extra)


def bytecode(accel=1, strict=0, **extra):
    return make_env("bytecode", accel=accel, strict=strict, **extra)


def token(**extra):
    return make_env("token", accel=0, strict=0, **extra)


def honest_tree(**extra):
    """Tree without accelerations (engine baseline)."""
    return tree(accel=0, **extra)


def honest_linear(**extra):
    return linear(accel=0, strict=0, **extra)
