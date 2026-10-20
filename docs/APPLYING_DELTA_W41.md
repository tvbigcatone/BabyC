# Applying W41 delta

Apply `DELTA_W41.patch` from the project root with:

```sh
patch -p1 < DELTA_W41.patch
```

The patch is relative to the available W40 tree. It does not include generated binaries or object files. See `docs/FP_IR_W41_TYPE_CLASSIFICATION.md` and `VERIFY_W41.txt` for scope and test evidence.
