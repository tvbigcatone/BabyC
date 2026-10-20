/* ir_lin.h — F5 linear form (three-address). Behind BABYC_LOWER=1.
 * Not a bytecode emitter. Shadow-compared to the tree executor. */
#ifndef BABYC_IR_LIN_H
#define BABYC_IR_LIN_H
#include "ir.h"

#define IR_LIN_MAX 48
#define IR_LIN_STMT_MAX 2048
#define IR_LIN_REGS 64
#define IR_LIN_CALL_SLOTS 64
#define IR_LIN_CALL_ARGS 16 /* = tree intrinsic limit (ir_call.c) */

/* Kernel kind codes in IrLinKernel.Tok */
#define IR_K_INSERT_SHIFT 1
#define IR_K_ARRAY_ACCUM 2
#define IR_K_ARRAY_FILL 3
#define IR_K_PTR_WALK 4
#define IR_K_KERNEL_FOR 5
#define IR_K_DO_COUNTDOWN 6
#define IR_K_DECL_ARRAY 7
#define IR_K_DECL_STRUCT 8
#define IR_K_DECL_STATIC 11
#define IR_K_DECL_STATIC_ARR 12
#define IR_K_DECL_STATIC_STU 13
#define IR_K_STRIDE_FILL 9
#define IR_K_SCAN_ACCUM 10
/* Shared tree loop fast-path chains (ir_fast.c); R[Dest]=0 on success. */
#define IR_K_TREE_WHILE 14
#define IR_K_TREE_FOR_PRE 15
#define IR_K_TREE_FOR 16

typedef enum {
    IrLinImm = 1,
    IrLinLoadLocal,
    IrLinStoreLocal,
    IrLinBin,
    IrLinUnary,
    IrLinJmp,
    IrLinBrZ,
    IrLinRet,
    IrLinLoadGlobal,
    IrLinStoreGlobal,
    IrLinLoadIndex,
    IrLinStoreIndex,
    IrLinLine,
    IrLinCall,
    IrLinKernel,
    IrLinIncG,     /* Imm=glob idx; Dest unused; A unused: global++ */
    IrLinAddI,     /* Dest = A + Imm (signed) */
    IrLinAddLocal, /* Locals[A] += Imm; Dest = new value */
    IrLinBrLT,     /* if A < B goto Imm; else fallthrough */
    IrLinAddStoreLocal, /* Dest = old Locals[A] + R[B]; then store result in A */
    IrLinCopyIndex,    /* copy p[R[A]] to p[R[B]]; Imm packs source/dest metadata */
    IrLinBrCmp,         /* if R[A] Tok R[B] goto Imm; Dest bit0=unsigned */
    IrLinAddRegLocal,   /* promoted slot: R[A]+=Imm, narrowed as local Tok; R[Dest]=result, B=slot */
    IrLinLoadMemberArray,  /* checked load from fixed array reached through member chain */
    IrLinStoreMemberArray, /* checked store to fixed array reached through member chain */
    IrLinStoreDeref,       /* checked typed store through an arbitrary pointer expression */
    IrLinVaArg             /* Dest = next promoted ABI payload from cursor in A; Imm=(intptr_t)requested ValueType* */
} IrLinOp;

/* Imm flags for IrLinBin / IrLinUnary (not Imm slot ops). */
#define IR_LIN_F_UNSIGNED  0x1
#define IR_LIN_F_WIDE      0x2  /* treat as unsigned long long width */
#define IR_LIN_F_FP        0x4  /* IEEE double bits in regs */

/* IrLinLoadIndex Tok modes (in addition to LexToken values).
 * Row decay returns &array[i][0] for a global multidimensional array. */
#define IR_LIN_IDX_ROW_DECAY 8

typedef struct IrLinInst {
    unsigned char Op;
    unsigned char Dest, A, B;
    enum LexToken Tok;
    long long Imm;
} IrLinInst;

/* Cached at startup (IrHookInit / first query). Zero cost when off. */
extern int BabycLowerOn;
extern int BabycDebugOn;
static inline int IrLowerEnabled(void) { return BabycLowerOn; }
void IrLowerEnabledInit(void); /* optional re-init after setenv */
int IrLowerExpr(IrExpr *E, IrLinInst *Code, int Cap, int *NOut, int *DestOut);
int IrLowerStmt(IrStmt *S, IrLinInst *Code, int Cap, int *NOut);
long long IrExecLin(struct ParseState *Parser, const IrLinInst *Code, int N, int Dest);
int IrExecLinLocals(const IrLinInst *Code, int N, long long *Locals, int NLocals);
int IrExecLinLocalsRegs(const IrLinInst *Code, int N, long long *Locals, int NLocals, int RegCount);
void IrPrintLowerStats(void);
/* Coverage: linear/VM instruction steps vs tree stmt/expr nodes. */
extern unsigned long long BabycLinInstExec;
extern unsigned long long BabycTreeStmtExec;
extern unsigned long long BabycTreeExprExec;
extern unsigned long long BabycFnCodeBodyRuns;
extern unsigned long long BabycTreeBodyRuns;
extern int BabycLinExecDepth;
int IrExecIsVmProd(void);
/* F6 shadow VM (ir_vm.c) — used from ir_lower when BABYC_EXEC=both|vm. */
void IrVmShadow(const IrLinInst *Code, int N, const long long *Before, int Depth);
void IrVmCommit(int Depth);
/* F8: production path under BABYC_EXEC=vm — dense words on real frame. */
int IrVmExecProd(const IrLinInst *Code, int N, long long *Locals, int NLocals);
/* Execute one preflight-approved local scalar-array declaration kernel. */
int IrLowerCallAggregatePureProd(int ci);
long long IrLowerVmStabilizeAggregateReturn(long long v);
int IrLowerVmIsAggregateReturnRange(long long v, size_t bytes);
int IrLowerVmExecArrayDecl(const IrLinInst *In, long long *Regs, long long *Locals, int NLocals);
void IrPrintVmStats(void);
/* Body → linear + optional VM (defined in ir_lower.c). */
int IrTryExecBodyVm(struct ParseState *Parser, IrStmt *Body);
int IrVmProfileOn(void);
#endif
