/* ir_lin_compile.h — linear compile: FnCodeCompile + LowerStmt + frames/labs.
 * PERF: compile-time only; no per-instruction getters on exec path.
 */
#ifndef BABYC_IR_LIN_COMPILE_H
#define BABYC_IR_LIN_COMPILE_H

#include "ir_fncode.h"
#include "ir_lin.h"
#include "interpreter.h"

FnCodeSlot *FnCodeCompile(struct ParseState *Parser, IrStmt *Body, struct Value *Func);
int IrLowerStmt(IrStmt *S, IrLinInst *Code, int Cap, int *NOut);
int CompileFramePush(void);
void CompileFramePop(void);

extern int NLab;
extern int LoopSp;
int IrFnBodyIntegerEligible(IrStmt *Body);
int IrExprIntegerOk(IrExpr *E);
/* Host Intrinsics the linear path can pack and call (eligibility + emit). */
int IrLinHostSupported(const char *Name);
int IrStmtIntegerOk(IrStmt *S);
#endif
