# W30 small fruit 269 — char *names[N] (in progress)

## Symptom
269: STRICT t=18 vs soft/tree t=15. names[i] returns wrong pointer (first='p', len=6).

## Architecture found
1. Light: DECL_ARRAY sets Fr->LocalVal but LoadLocal reads LLoc — stamped LightLocal on alloc.
2. aop load: CompLocalFromType of T* has FromType=T (char Sizeof=1); must not use as element stride — force es=sizeof(void*) for array-of-ptr.
3. DeclFromType=NULL on StarPtr arrays in ir_compile — relies on CompLocalLeafEs/ElemIsPtr.
4. Residual still open: store/load of pointer words into local T*[] still yields wrong values under STRICT. Soft uses tree Decl residual.

## Next
Runtime dump of Tok=2 store addr/value and subsequent load for names[0]=n0.
