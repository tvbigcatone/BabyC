/* ir_private.h — shared between parse.c (compile) and ir_exec.c (execute)
 *
 * FastForCond / FastForInc / IrCallFrame live ONLY in ir_runtime.h.
 * Do not re-typedef them here (MSVC C2011 on Win32).
 */
#ifndef BABYC_IR_PRIVATE_H
#define BABYC_IR_PRIVATE_H

#include "interpreter.h"
#include "ir_runtime.h" /* B.3: frame + pure runtime surface (single definition) */

/* Fast-for helpers (impl split parse/ir_exec) */
int FastForCaptureCond(struct ParseState *From, FastForCond *Out);
int FastForCaptureInc(struct ParseState *From, FastForInc *Out);
PTR_TYPE_INT_SIGNED FastForEvalCond(struct ParseState *Parser, FastForCond *C);
void FastForDoInc(struct ParseState *Parser, FastForInc *Inc);
int IrTryTightFor(struct ParseState *Parser, IrStmt *S);

/* IrbTryLoop retired (PIANO_TOKEN_PATH_V2 P1) */
int IrOpPrecedence(enum LexToken Op);

#endif /* BABYC_IR_PRIVATE_H */
