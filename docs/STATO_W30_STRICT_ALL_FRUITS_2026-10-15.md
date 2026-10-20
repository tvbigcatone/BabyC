# W30 STRICT — cesto completo (2026-10-15)

## Copertura
- STRICT: **832/930 = 89.5%** (fb=68, sil=30)
- SOFT: **920/930 = 98.9%**

## Frutti chiusi (tutti nel delta)

### FP
- 950 scaled(k) Cast Imm=2 Light/LinStep/vm
- 953 fill i*0.5 / sum*n BinOp L+R promote
- 956 a[i]=i owned_arr is_ptr exclude + Index FP convert
- antireg 1005–1007, 1009

### Unsigned
- 792/1008 *(unsigned arr+i)>>n IR_LIN_F_UNSIGNED

### Scale / pointer arithmetic (TROPHY cluster)
- **625** nested FnCodeCompile caller LocalIsPtr[slot] false scale *4
- **178/177/163/188/345/347/351/390/409/414/749/751**
  - scalar pointer Decl → CompLocalFromType + SlotIsPtr
  - scale gate: SlotIsPtr / CompLocalNDims / LeafEs (array decay a+N)
  - scale from CompLocalLeafEs when ElemSize missing
- antireg 1010, 1011

## Architettura
1. Cast Imm=2 su tutti gli interpreti linear
2. owned array ≠ Tok=2 pointer store
3. LinCompileActive: non ereditare LocalIsPtr del caller
4. Array Decl peels CT to element — usare NDims/LeafEs per decay scale
5. Pointer Decl must stamp compile-time meta for p+1

## Residual silenti rimanenti (30)
954 FP return int, nested member assign, args harness, analyze/crash, fptr nested, bitfield nested, …
