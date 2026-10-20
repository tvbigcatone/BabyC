/* ir_lin_emit.h — AllocReg/Emit/LowerRec (expression lower to linear).
 * PERF: compile-time only.
 */
#ifndef BABYC_IR_LIN_EMIT_H
#define BABYC_IR_LIN_EMIT_H

#include "ir_lin.h"
#include "interpreter.h"

int AllocReg(int *Next);
int Emit(IrLinInst *Code, int Cap, int *N, IrLinInst In);
int EmitLine(IrLinInst *Code, int Cap, int *N, int Line);
int LowerRec(IrExpr *E, IrLinInst *Code, int Cap, int *N, int *Next, int *Dest);
int IrLinEmitScalarConversion(enum BaseType TargetBase, IrExpr *Source,
                              int *Reg, IrLinInst *Code, int Cap,
                              int *N, int *Next);
int GlobPush(IrExpr *E);
int IsRel(enum LexToken Op);

int IrLinPeephole(IrLinInst *Code, int N);
#endif
