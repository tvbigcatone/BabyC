/* ir_lin_exec.h — linear Step + IrExecLinLocals (hot path).
 * PERF: direct array/reg access only; no non-inline getters.
 */
#ifndef BABYC_IR_LIN_EXEC_H
#define BABYC_IR_LIN_EXEC_H

#include "ir_lin.h"

void Step(const IrLinInst *In, long long *R, long long *Locals, int NLocals, int *Ip);
int LinExecIndexCopy(const IrLinInst *In, long long *R, long long *Locals, int NLocals, int *Ip);
int IrExecLinLocals(const IrLinInst *Code, int N, long long *Locals, int NLocals);
int IrExecLinLocalsRegs(const IrLinInst *Code, int N, long long *Locals, int NLocals, int RegCount);

#endif
