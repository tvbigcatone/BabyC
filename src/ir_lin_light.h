/* ir_lin_light.h — light-window interpreter.
 * PERF: Light* arrays are direct globals — no non-inline getters.
 */
#ifndef BABYC_IR_LIN_LIGHT_H
#define BABYC_IR_LIN_LIGHT_H

#include "ir_fncode.h"
#include "ir_runtime.h"

#define LIGHT_MAX 512
#define LIGHT_LOCALS 32

extern long long LightParam[LIGHT_MAX][IR_CALL_MAX_PARAMS];
extern long long LightLocal[LIGHT_MAX][LIGHT_LOCALS];
extern unsigned long long LightInitMask[LIGHT_MAX];
extern int LightSp;

long long LightRunBody(FnCodeSlot *CS, int lsp);
long long LightCallSelf(FnCodeSlot *CS, struct Value *FV, long long *args, int na);

int IrFnCodeHasPtrSlots(FnCodeSlot *CS);
int IrTryKernelForLight(IrStmt *S, long long *LLoc, long long *LParam,
                       int nloc, int nparam, int param_bound_hint);

#endif
