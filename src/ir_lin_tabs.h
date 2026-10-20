/* ir_lin_tabs.h — live linear Call/Glob/Idx/Kernel/Wide tables.
 *
 * PERF: these are plain global arrays. Hot path (Step/Light) indexes them
 * directly. Do NOT wrap in non-inline getters.
 */
#ifndef BABYC_IR_LIN_TABS_H
#define BABYC_IR_LIN_TABS_H

#include "ir_lin.h"
#include "interpreter.h"

struct Value;
struct ValueType;
struct IrExpr;
struct IrStmt;

extern struct Value *CallFnTab[IR_LIN_CALL_SLOTS];
extern int CallIndirectSlot[IR_LIN_CALL_SLOTS];
extern int CallIndirectReg[IR_LIN_CALL_SLOTS];
extern int CallIndirectParam[IR_LIN_CALL_SLOTS];
extern IrExpr *CallIndirectExpr[IR_LIN_CALL_SLOTS];
extern int CallCharPos[IR_LIN_CALL_SLOTS];
extern int CallArgReg[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern int CallArgN[IR_LIN_CALL_SLOTS];
extern unsigned char CallArgIsPtr[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern unsigned char CallArgIsFp[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern unsigned char CallArgTypeBase[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern struct ValueType *CallArgType[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern long long CallStrBits[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern unsigned char CallStrOn[IR_LIN_CALL_SLOTS][IR_LIN_CALL_ARGS];
extern int CallN;

extern IrExpr *GlobTab[32];
extern struct Value *GlobVal[32];
extern long long *GlobRaw[32];
extern int GlobN;

extern IrExpr *IdxTab[32];
extern signed char IdxMetaSlot[32];
extern unsigned char IdxMetaKind[32];
extern int IdxMetaEs[32];
extern int IdxMetaN[32];
extern int IdxN;
extern unsigned IdxDirty;

extern IrStmt *KernelStmtTab[16];
extern int KernelN;

extern long long LinWideImm[IR_LIN_STMT_MAX];
extern unsigned char LinWideOn[IR_LIN_STMT_MAX];
extern int LinWideMax;

/* Defined in ir_lower.c — clears IdxGlobBase/IdxDirty after table install. */
void IdxRunClearCache(void);
void IdxRunClearCacheN(int n);

typedef struct IdxRunSave {
    unsigned Dirty;
    void *GlobBase[32];
    void *Addr[32];
    long long Save[32];
    int Es[32];
    int Eb[32];
} IdxRunSave;

void IdxRunPush(IdxRunSave *S);
void IdxRunPop(const IdxRunSave *S);

#endif /* BABYC_IR_LIN_TABS_H */
