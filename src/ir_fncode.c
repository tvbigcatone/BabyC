/* ir_fncode.c — FnCode slot registry (Find only in step 1).
 * Install/Snapshot/Compile remain in ir_lower.c until Call/Glob tables
 * are owned by a shared module. Field access stays direct everywhere.
 */
#include "ir_fncode.h"
#include "ir_lin_tabs.h"
#include "ir_runtime.h"
#include <string.h>

FnCodeSlot FnSlots[FNCODE_SLOTS];
int FnSlotN;
FnCodeSlot *FnActive;
FnCodeSlot *CompileSlot;

FnCodeSlot *FnCodeFind(IrStmt *Body)
{
    int i;
    for (i = 0; i < FnSlotN; i++)
        if (FnSlots[i].Body == Body)
            return &FnSlots[i];
    return NULL;
}
FnCodeSlot *FnCodeFindByFunc(struct Value *FV)
{
    int i;
    FnCodeSlot *S;
    if (FV == NULL) return NULL;
    for (i = 0; i < FnSlotN; i++)
        if (FnSlots[i].Func == FV && FnSlots[i].Elig == 1 && FnSlots[i].N > 0)
            return &FnSlots[i];
    if (FV->Val != NULL)
    {
        S = FnCodeFind((IrStmt *)FV->Val->FuncDef.BodyIr);
        if (S != NULL) { S->Func = FV; return S; }
    }
    return NULL;
}


/* ---- Install / Snapshot / Restore (cold path) ---- */

void FnCodeInstall(FnCodeSlot *S)
{
    int i, a;
    if (S == NULL) return;
    if (FnActive == S)
    {
        /* Same-body recursion: tables already live. Invalidate only this
         * function's indexed-memory slots; clearing all 32 entries on every
         * recursive invocation was unnecessary hot-path work. */
        IdxRunClearCacheN(S->IdxN);
        return;
    }
    {
        int n = S->CallN;
        CallN = n;
        for (i = 0; i < n; i++)
        {
            CallFnTab[i] = S->CallFnTab[i];
            CallIndirectSlot[i] = S->CallIndirectSlot[i];
            CallIndirectReg[i] = S->CallIndirectReg[i];
            CallIndirectParam[i] = S->CallIndirectParam[i];
            CallIndirectExpr[i] = S->CallIndirectExpr[i];
            CallArgN[i] = S->CallArgN[i];
            CallCharPos[i] = S->CallCharPos[i];
            for (a = 0; a < S->CallArgN[i] && a < IR_LIN_CALL_ARGS; a++)
            {
                CallArgReg[i][a] = S->CallArgReg[i][a];
                CallArgIsPtr[i][a] = S->CallArgIsPtr[i][a];
                CallArgIsFp[i][a] = S->CallArgIsFp[i][a];
                CallArgTypeBase[i][a] = S->CallArgTypeBase[i][a];
                CallArgType[i][a] = S->CallArgType[i][a];
                CallStrBits[i][a] = S->CallStrBits[i][a];
                CallStrOn[i][a] = S->CallStrOn[i][a];
            }
        }
        n = S->GlobN;
        GlobN = n;
        for (i = 0; i < n; i++)
        {
            GlobTab[i] = S->GlobTab[i];
            GlobVal[i] = S->GlobVal[i];
        }
        n = S->IdxN;
        IdxN = n;
        for (i = 0; i < n && i < 32; i++)
        {
            IdxTab[i] = S->IdxTab[i];
            IdxMetaSlot[i] = S->IdxMetaSlot[i];
            IdxMetaKind[i] = S->IdxMetaKind[i];
            IdxMetaEs[i] = S->IdxMetaEs[i];
            IdxMetaN[i] = S->IdxMetaN[i];
        }
        /* Base cache is per-frame runtime, not part of FnCodeSlot. */
        IdxRunClearCache();
        KernelN = S->KernelN;
        for (i = 0; i < 16; i++)
            KernelStmtTab[i] = S->KernelStmtTab[i];
        {
            int wm = S->WideMax;
            int oldwm = LinWideMax;
            if (wm > IR_LIN_STMT_MAX) wm = IR_LIN_STMT_MAX;
            if (wm > 0)
            {
                memcpy(LinWideImm, S->WideImm, (size_t)wm * sizeof(long long));
                memcpy(LinWideOn, S->WideOn, (size_t)wm);
            }
            /* Bytes beyond the previous high-water mark are already zero;
             * clear only the tail exposed by shrinking, not all 2048 entries
             * on every cross-function call. */
            if (oldwm > wm)
                memset(LinWideOn + wm, 0, (size_t)(oldwm - wm));
            LinWideMax = wm;
        }
    }
    /* Apply Decl names to the live frame (uninit diagnostics). */
    if (IrCallDepth > 0)
    {
        IrCallFrame *Fr = IrCallStack[IrCallDepth - 1];
        int si;
        for (si = 0; si < IR_CALL_MAX_LOCALS; si++)
            if (S->SlotName[si] != NULL)
                Fr->LocalName[si] = (char *)(void *)S->SlotName[si];
    }
    FnActive = S;
}


void FnCodeSnapshot(FnCodeSlot *S)
{
    int i, a, n;
    n = CallN;
    if (n > IR_LIN_CALL_SLOTS) n = IR_LIN_CALL_SLOTS;
    S->CallN = n;
    for (i = 0; i < n; i++)
    {
        S->CallFnTab[i] = CallFnTab[i];
        S->CallIndirectSlot[i] = CallIndirectSlot[i];
        S->CallIndirectReg[i] = CallIndirectReg[i];
        S->CallIndirectParam[i] = CallIndirectParam[i];
        S->CallIndirectExpr[i] = CallIndirectExpr[i];
        S->CallArgN[i] = CallArgN[i];
        S->CallCharPos[i] = CallCharPos[i];
        /* Only the call's own arguments: entries past CallArgN are never
         * read, and IR_LIN_CALL_ARGS (16) would double the copy cost. */
        for (a = 0; a < CallArgN[i] && a < IR_LIN_CALL_ARGS; a++)
        {
            S->CallArgReg[i][a] = CallArgReg[i][a];
            S->CallArgIsPtr[i][a] = CallArgIsPtr[i][a];
            S->CallArgIsFp[i][a] = CallArgIsFp[i][a];
            S->CallArgTypeBase[i][a] = CallArgTypeBase[i][a];
            S->CallArgType[i][a] = CallArgType[i][a];
            S->CallStrBits[i][a] = CallStrBits[i][a];
            S->CallStrOn[i][a] = CallStrOn[i][a];
        }
    }
    n = GlobN;
    if (n > 32) n = 32;
    S->GlobN = n;
    for (i = 0; i < n; i++)
    {
        S->GlobTab[i] = GlobTab[i];
        S->GlobVal[i] = GlobVal[i];
    }
    n = IdxN;
    if (n > 32) n = 32;
    S->IdxN = n;
    for (i = 0; i < n; i++)
    {
        S->IdxTab[i] = IdxTab[i];
        S->IdxMetaSlot[i] = IdxMetaSlot[i];
        S->IdxMetaKind[i] = IdxMetaKind[i];
        S->IdxMetaEs[i] = IdxMetaEs[i];
        S->IdxMetaN[i] = IdxMetaN[i];
    }
    n = KernelN;
    if (n > 16) n = 16;
    S->KernelN = n;
    for (i = 0; i < n; i++)
        S->KernelStmtTab[i] = KernelStmtTab[i];
    {
        int wm = LinWideMax;
        if (wm > IR_LIN_STMT_MAX) wm = IR_LIN_STMT_MAX;
        S->WideMax = wm;
        if (wm > 0)
        {
            memcpy(S->WideImm, LinWideImm, (size_t)wm * sizeof(long long));
            memcpy(S->WideOn, LinWideOn, (size_t)wm);
        }
        if (wm < IR_LIN_STMT_MAX)
            memset(S->WideOn + wm, 0, (size_t)(IR_LIN_STMT_MAX - wm));
    }
}


void LinTabsRestore(const FnCodeSlot *S)
{
    int i, a, n;
    if (S == NULL)
        return;
    n = S->CallN;
    if (n > IR_LIN_CALL_SLOTS) n = IR_LIN_CALL_SLOTS;
    CallN = n;
    for (i = 0; i < n; i++)
    {
        CallFnTab[i] = S->CallFnTab[i];
        CallIndirectSlot[i] = S->CallIndirectSlot[i];
        CallIndirectReg[i] = S->CallIndirectReg[i];
        CallIndirectParam[i] = S->CallIndirectParam[i];
        CallIndirectExpr[i] = S->CallIndirectExpr[i];
        CallArgN[i] = S->CallArgN[i];
        CallCharPos[i] = S->CallCharPos[i];
        for (a = 0; a < S->CallArgN[i] && a < IR_LIN_CALL_ARGS; a++)
        {
            CallArgReg[i][a] = S->CallArgReg[i][a];
            CallArgIsPtr[i][a] = S->CallArgIsPtr[i][a];
            CallArgIsFp[i][a] = S->CallArgIsFp[i][a];
                CallArgTypeBase[i][a] = S->CallArgTypeBase[i][a];
                CallArgType[i][a] = S->CallArgType[i][a];
            CallStrBits[i][a] = S->CallStrBits[i][a];
            CallStrOn[i][a] = S->CallStrOn[i][a];
        }
    }
    n = S->GlobN;
    if (n > 32) n = 32;
    GlobN = n;
    for (i = 0; i < n; i++)
    {
        GlobTab[i] = S->GlobTab[i];
        GlobVal[i] = S->GlobVal[i];
    }
    n = S->IdxN;
    if (n > 32) n = 32;
    IdxN = n;
    for (i = 0; i < n; i++)
    {
        IdxTab[i] = S->IdxTab[i];
        IdxMetaSlot[i] = S->IdxMetaSlot[i];
        IdxMetaKind[i] = S->IdxMetaKind[i];
        IdxMetaEs[i] = S->IdxMetaEs[i];
        IdxMetaN[i] = S->IdxMetaN[i];
    }
    n = S->KernelN;
    if (n > 16) n = 16;
    KernelN = n;
    for (i = 0; i < n; i++)
        KernelStmtTab[i] = S->KernelStmtTab[i];
    {
        int wm = S->WideMax;
        int oldwm = LinWideMax;
        if (wm > IR_LIN_STMT_MAX) wm = IR_LIN_STMT_MAX;
        if (wm > 0)
        {
            memcpy(LinWideImm, S->WideImm, (size_t)wm * sizeof(long long));
            memcpy(LinWideOn, S->WideOn, (size_t)wm);
        }
        if (oldwm > wm)
            memset(LinWideOn + wm, 0, (size_t)(oldwm - wm));
        LinWideMax = wm;
    }
    IdxRunClearCache();
}

