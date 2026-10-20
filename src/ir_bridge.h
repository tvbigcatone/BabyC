/* ir_bridge.h — residual Variable / Table / Heap / Expression edge for Body-IR
 * Used for: globals, function resolve, Intrinsic Value* pack, dual-write shadow,
 * token interop. Not the hot path for LocalSlot/ParamSlot >= 0.
 */
#ifndef BABYC_IR_BRIDGE_H
#define BABYC_IR_BRIDGE_H

#include "interpreter.h"

/* VariableGet / VariableDefine / TableGet / Expression* / Heap* come from
 * interpreter.h + their .c units. This header exists so ir_exec documents
 * the boundary: anything that still needs Value* goes through the bridge.
 */

#endif /* BABYC_IR_BRIDGE_H */
