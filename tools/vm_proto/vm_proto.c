/* BabyC F1 — register VM prototype (standalone).
 * gcc -O2 -o vm_proto vm_proto.c -lm
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#define MAX_CODE  8192
#define NREG      16
#define MAX_FRAME 65536
#define MAX_HEAP  128

typedef enum {
    OP_NOP=0, OP_MOVI, OP_MOV, OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD,
    OP_ADDF, OP_SUBF, OP_MULF, OP_DIVF,
    OP_LOAD, OP_STORE, OP_LOADC, OP_STOREC, OP_LOADF, OP_STOREF,
    OP_CMP_LT, OP_CMP_EQ, OP_JMP, OP_JNZ, OP_JZ,
    OP_CALL, OP_RET, OP_PRINT_I, OP_PRINT_F, OP_ALLOC, OP_FREE, OP_HALT
} Op;

typedef struct { uint8_t op; int8_t a, b, c; int32_t imm; } Ins;
typedef struct { void *ptr; size_t size; int live; } HeapBlk;
typedef union { long long ival; double dval; } Slot;

static Ins code[MAX_CODE];
static int ncode;
static Slot frames[MAX_FRAME][NREG];
static int fp, pc, ret_pc[MAX_FRAME];
static HeapBlk heap[MAX_HEAP];
static int nheap, fault;

static int emit(Op op, int a, int b, int c, int imm) {
    code[ncode] = (Ins){ (uint8_t)op, (int8_t)a, (int8_t)b, (int8_t)c, imm };
    return ncode++;
}

static int heap_find(void *p) {
    int i;
    for (i = 0; i < nheap; i++)
        if (heap[i].live && heap[i].ptr == p) return i;
    return -1;
}
static void *vm_alloc(size_t sz) {
    void *p;
    if (nheap >= MAX_HEAP) return NULL;
    p = malloc(sz);
    if (!p) return NULL;
    heap[nheap++] = (HeapBlk){ p, sz, 1 };
    return p;
}
static int vm_free(void *p) {
    int i = heap_find(p);
    if (i < 0) return -1;
    free(heap[i].ptr);
    heap[i].live = 0;
    heap[i].ptr = NULL;
    return 0;
}

static int run(int entry) {
    pc = entry;
    fp = 0;
    fault = 0;
    for (;;) {
        Ins *ins = &code[pc++];
        switch (ins->op) {
        case OP_NOP: break;
        case OP_MOVI: frames[fp][ins->a].ival = ins->imm; break;
        case OP_MOV:  frames[fp][ins->a] = frames[fp][ins->b]; break;
        case OP_ADD:  frames[fp][ins->a].ival = frames[fp][ins->b].ival + frames[fp][ins->c].ival; break;
        case OP_SUB:  frames[fp][ins->a].ival = frames[fp][ins->b].ival - frames[fp][ins->c].ival; break;
        case OP_MUL:  frames[fp][ins->a].ival = frames[fp][ins->b].ival * frames[fp][ins->c].ival; break;
        case OP_DIV:
            if (frames[fp][ins->c].ival == 0) { fault = 3; return -4; }
            frames[fp][ins->a].ival = frames[fp][ins->b].ival / frames[fp][ins->c].ival; break;
        case OP_MOD:
            if (frames[fp][ins->c].ival == 0) { fault = 3; return -4; }
            frames[fp][ins->a].ival = frames[fp][ins->b].ival % frames[fp][ins->c].ival; break;
        case OP_ADDF: frames[fp][ins->a].dval = frames[fp][ins->b].dval + frames[fp][ins->c].dval; break;
        case OP_SUBF: frames[fp][ins->a].dval = frames[fp][ins->b].dval - frames[fp][ins->c].dval; break;
        case OP_MULF: frames[fp][ins->a].dval = frames[fp][ins->b].dval * frames[fp][ins->c].dval; break;
        case OP_DIVF: frames[fp][ins->a].dval = frames[fp][ins->b].dval / frames[fp][ins->c].dval; break;
        case OP_LOAD:
            frames[fp][ins->a].ival =
                ((long long *)(intptr_t)frames[fp][ins->b].ival)[frames[fp][ins->c].ival];
            break;
        case OP_STORE:
            ((long long *)(intptr_t)frames[fp][ins->b].ival)[frames[fp][ins->c].ival] =
                frames[fp][ins->a].ival;
            break;
        case OP_LOADF:
            frames[fp][ins->a].dval =
                ((double *)(intptr_t)frames[fp][ins->b].ival)[frames[fp][ins->c].ival];
            break;
        case OP_STOREF:
            ((double *)(intptr_t)frames[fp][ins->b].ival)[frames[fp][ins->c].ival] =
                frames[fp][ins->a].dval;
            break;
        case OP_LOADC: {
            char *base = (char *)(intptr_t)frames[fp][ins->b].ival;
            long long idx = frames[fp][ins->c].ival;
            int hi = heap_find(base);
            size_t off = (size_t)idx * sizeof(long long);
            if (hi < 0 || off + sizeof(long long) > heap[hi].size) { fault = 1; return -2; }
            frames[fp][ins->a].ival = ((long long *)base)[idx];
            break;
        }
        case OP_STOREC: {
            char *base = (char *)(intptr_t)frames[fp][ins->b].ival;
            long long idx = frames[fp][ins->c].ival;
            int hi = heap_find(base);
            size_t off = (size_t)idx * sizeof(long long);
            if (hi < 0 || off + sizeof(long long) > heap[hi].size) { fault = 1; return -2; }
            ((long long *)base)[idx] = frames[fp][ins->a].ival;
            break;
        }
        case OP_CMP_LT:
            frames[fp][ins->a].ival = frames[fp][ins->b].ival < frames[fp][ins->c].ival;
            break;
        case OP_CMP_EQ:
            frames[fp][ins->a].ival = frames[fp][ins->b].ival == frames[fp][ins->c].ival;
            break;
        case OP_JMP: pc = ins->imm; break;
        case OP_JNZ: if (frames[fp][ins->a].ival) pc = ins->imm; break;
        case OP_JZ:  if (!frames[fp][ins->a].ival) pc = ins->imm; break;
        case OP_CALL: {
            long long arg = frames[fp][ins->a].ival;
            if (fp + 1 >= MAX_FRAME) return -1;
            ret_pc[fp] = pc;
            fp++;
            frames[fp][0].ival = arg;
            frames[fp][1].ival = frames[fp][2].ival = frames[fp][3].ival = 0;
            pc = ins->imm;
            break;
        }
        case OP_RET: {
            long long ret = frames[fp][0].ival;
            if (fp <= 0) { frames[0][0].ival = ret; return 0; }
            fp--;
            pc = ret_pc[fp];
            frames[fp][0].ival = ret;
            break;
        }
        case OP_PRINT_I: printf("%lld\n", frames[fp][ins->a].ival); break;
        case OP_PRINT_F: printf("%.6f\n", frames[fp][ins->a].dval); break;
        case OP_ALLOC:
            frames[fp][ins->a].ival =
                (long long)(intptr_t)vm_alloc((size_t)frames[fp][ins->b].ival);
            break;
        case OP_FREE:
            if (vm_free((void *)(intptr_t)frames[fp][ins->a].ival) < 0) {
                fault = 2; return -3;
            }
            break;
        case OP_HALT: return 0;
        default: return -9;
        }
    }
}

static int build_fib(void) {
    int entry, jz_patch;
    ncode = 0;
    entry = ncode;
    emit(OP_MOVI, 2, 0, 0, 2);
    emit(OP_CMP_LT, 3, 0, 2, 0);
    jz_patch = emit(OP_JZ, 3, 0, 0, 0);
    emit(OP_RET, 0, 0, 0, 0);
    code[jz_patch].imm = ncode;
    emit(OP_MOVI, 4, 0, 0, 1);
    emit(OP_SUB, 1, 0, 4, 0);
    emit(OP_MOV, 5, 0, 0, 0);
    emit(OP_CALL, 1, 0, 0, entry);
    emit(OP_MOV, 6, 0, 0, 0);
    emit(OP_MOVI, 4, 0, 0, 2);
    emit(OP_SUB, 1, 5, 4, 0);
    emit(OP_CALL, 1, 0, 0, entry);
    emit(OP_ADD, 0, 6, 0, 0);
    emit(OP_RET, 0, 0, 0, 0);
    return entry;
}

static int build_float_sum(int N) {
    int L, Ldone;
    ncode = 0;
    emit(OP_MOVI, 0, 0, 0, 0);
    emit(OP_MOVI, 1, 0, 0, 0);
    emit(OP_MOVI, 2, 0, 0, N);
    L = ncode;
    emit(OP_CMP_LT, 5, 1, 2, 0);
    Ldone = emit(OP_JZ, 5, 0, 0, 0);
    emit(OP_ADDF, 0, 0, 3, 0); /* sum += R3 (1.0) */
    emit(OP_MOVI, 7, 0, 0, 1);
    emit(OP_ADD, 1, 1, 7, 0);
    emit(OP_JMP, 0, 0, 0, L);
    code[Ldone].imm = ncode;
    emit(OP_HALT, 0, 0, 0, 0);
    return 0;
}

static int build_array_sum(int N, int checked) {
    int L_fill, L_fd, L_sum, L_sd;
    ncode = 0;
    emit(OP_MOVI, 1, 0, 0, N * (int)sizeof(long long));
    emit(OP_ALLOC, 2, 1, 0, 0);
    emit(OP_MOVI, 3, 0, 0, 0);
    emit(OP_MOVI, 4, 0, 0, N);
    L_fill = ncode;
    emit(OP_CMP_LT, 5, 3, 4, 0);
    L_fd = emit(OP_JZ, 5, 0, 0, 0);
    emit(OP_MOV, 0, 3, 0, 0);
    emit(checked ? OP_STOREC : OP_STORE, 0, 2, 3, 0);
    emit(OP_MOVI, 6, 0, 0, 1);
    emit(OP_ADD, 3, 3, 6, 0);
    emit(OP_JMP, 0, 0, 0, L_fill);
    code[L_fd].imm = ncode;
    emit(OP_MOVI, 0, 0, 0, 0);
    emit(OP_MOVI, 3, 0, 0, 0);
    L_sum = ncode;
    emit(OP_CMP_LT, 5, 3, 4, 0);
    L_sd = emit(OP_JZ, 5, 0, 0, 0);
    emit(checked ? OP_LOADC : OP_LOAD, 7, 2, 3, 0);
    emit(OP_ADD, 0, 0, 7, 0);
    emit(OP_MOVI, 6, 0, 0, 1);
    emit(OP_ADD, 3, 3, 6, 0);
    emit(OP_JMP, 0, 0, 0, L_sum);
    code[L_sd].imm = ncode;
    emit(OP_FREE, 2, 0, 0, 0);
    emit(OP_HALT, 0, 0, 0, 0);
    return 0;
}

static int build_heap_oob(void) {
    ncode = 0;
    emit(OP_MOVI, 1, 0, 0, 16); emit(OP_ALLOC, 2, 1, 0, 0);
    emit(OP_MOVI, 0, 0, 0, 99); emit(OP_MOVI, 3, 0, 0, 2);
    emit(OP_STOREC, 0, 2, 3, 0); emit(OP_HALT, 0, 0, 0, 0);
    return 0;
}
static int build_double_free(void) {
    ncode = 0;
    emit(OP_MOVI, 1, 0, 0, 16); emit(OP_ALLOC, 2, 1, 0, 0);
    emit(OP_FREE, 2, 0, 0, 0); emit(OP_FREE, 2, 0, 0, 0); emit(OP_HALT, 0, 0, 0, 0);
    return 0;
}


static int build_checksum(int N, int checked) {
    int L_fill, L_fd, L_sum, L_sd;
    Op ST = checked ? OP_STOREC : OP_STORE;
    Op LD = checked ? OP_LOADC : OP_LOAD;
    ncode = 0;
    emit(OP_MOVI, 1, 0, 0, N * (int)sizeof(long long));
    emit(OP_ALLOC, 2, 1, 0, 0);
    emit(OP_MOVI, 3, 0, 0, 0);
    emit(OP_MOVI, 4, 0, 0, N);
    L_fill = ncode;
    emit(OP_CMP_LT, 5, 3, 4, 0);
    L_fd = emit(OP_JZ, 5, 0, 0, 0);
    emit(OP_SUB, 0, 4, 3, 0);
    emit(ST, 0, 2, 3, 0);
    emit(OP_MOVI, 6, 0, 0, 1);
    emit(OP_ADD, 3, 3, 6, 0);
    emit(OP_JMP, 0, 0, 0, L_fill);
    code[L_fd].imm = ncode;
    emit(OP_MOVI, 0, 0, 0, 0);
    emit(OP_MOVI, 3, 0, 0, 0);
    L_sum = ncode;
    emit(OP_CMP_LT, 5, 3, 4, 0);
    L_sd = emit(OP_JZ, 5, 0, 0, 0);
    emit(LD, 7, 2, 3, 0);
    emit(OP_ADD, 0, 0, 7, 0);
    emit(OP_MOVI, 6, 0, 0, 1);
    emit(OP_ADD, 3, 3, 6, 0);
    emit(OP_JMP, 0, 0, 0, L_sum);
    code[L_sd].imm = ncode;
    emit(OP_FREE, 2, 0, 0, 0);
    emit(OP_HALT, 0, 0, 0, 0);
    return 0;
}

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "all";
    int runs = 5, r;
    if (!strcmp(mode, "fib") || !strcmp(mode, "all")) {
        int entry = build_fib();
        double best = 1e9;
        long long result = 0;
        for (r = 0; r < runs; r++) {
            memset(frames[0], 0, sizeof frames[0]);
            frames[0][0].ival = 30;
            double t0 = now();
            run(entry);
            result = frames[0][0].ival;
            double t1 = now();
            if (t1 - t0 < best) best = t1 - t0;
        }
        printf("fib(30) result=%lld best=%.4fs %s\n", result, best,
               best <= 0.05 ? "GO" : (best <= 0.15 ? "NEAR" : "STOP"));
    }
    if (!strcmp(mode, "array") || !strcmp(mode, "all")) {
        int N = 200000, c;
        for (c = 0; c <= 1; c++) {
            build_array_sum(N, c);
            double best = 1e9;
            long long result = 0;
            for (r = 0; r < runs; r++) {
                int i;
                for (i = 0; i < nheap; i++)
                    if (heap[i].live) { free(heap[i].ptr); heap[i].live = 0; }
                nheap = 0;
                double t0 = now();
                run(0);
                result = frames[0][0].ival;
                double t1 = now();
                if (t1 - t0 < best) best = t1 - t0;
            }
            printf("array_sum N=%d checked=%d result=%lld best=%.4fs\n", N, c, result, best);
        }
    }
    if (!strcmp(mode, "float") || !strcmp(mode, "all")) {
        int N = 2000000;
        build_float_sum(N);
        double best = 1e9, result = 0;
        for (r = 0; r < runs; r++) {
            memset(frames[0], 0, sizeof frames[0]);
            frames[0][3].dval = 1.0;
            double t0 = now();
            run(0);
            result = frames[0][0].dval;
            double t1 = now();
            if (t1 - t0 < best) best = t1 - t0;
        }
        printf("float_sum N=%d result=%.1f best=%.4fs\n", N, result, best);
    }
    if (!strcmp(mode, "printf") || !strcmp(mode, "all")) {
        int entry = build_fib();
        double best = 1e9;
        for (r = 0; r < runs; r++) {
            memset(frames[0], 0, sizeof frames[0]);
            frames[0][0].ival = 30;
            double t0 = now();
            run(entry);
            printf("%lld\n", frames[0][0].ival);
            double t1 = now();
            if (t1 - t0 < best) best = t1 - t0;
        }
        printf("fib+printf best=%.4fs\n", best);
    }

    if (!strcmp(mode, "checksum") || !strcmp(mode, "all")) {
        int N = 100000, c;
        for (c = 0; c <= 1; c++) {
            build_checksum(N, c);
            double best = 1e9;
            long long result = 0;
            for (r = 0; r < runs; r++) {
                int i;
                for (i = 0; i < nheap; i++)
                    if (heap[i].live) { free(heap[i].ptr); heap[i].live = 0; }
                nheap = 0;
                double t0 = now();
                run(0);
                result = frames[0][0].ival;
                double t1 = now();
                if (t1 - t0 < best) best = t1 - t0;
            }
            printf("checksum N=%d checked=%d result=%lld best=%.4fs\n", N, c, result, best);
        }
    }
    if (!strcmp(mode, "heap") || !strcmp(mode, "all")) {
        int rc, i;
        for (i = 0; i < nheap; i++)
            if (heap[i].live) { free(heap[i].ptr); heap[i].live = 0; }
        nheap = 0;
        build_heap_oob();
        rc = run(0);
        printf("heap_oob rc=%d fault=%d\n", rc, fault);
        for (i = 0; i < nheap; i++)
            if (heap[i].live) { free(heap[i].ptr); heap[i].live = 0; }
        nheap = 0;
        build_double_free();
        rc = run(0);
        printf("double_free rc=%d fault=%d\n", rc, fault);
    }
    return 0;
}
