# Applying W43

Apply `DELTA_W43.patch` from the project root after W42:

```sh
patch -p1 < DELTA_W43.patch
```

W43 is a preparatory source change, not a complete float/double implementation. See `VERIFY_W43.txt` and `docs/FP_IR_W43_BINARY32_LVALUE_PROXY.md`.
