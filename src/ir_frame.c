/* ir_frame.c — Body-IR call frame lifecycle (D1 extract from ir_exec.c)
 *
 * Owns: frame pool/stack, push/pop, bump arena, owned-local teardown,
 *       static-local persist table, token-call frame snapshot/restore.
 * No VariableGet. Bridge: ProgramFail only.
 */
#include "picoc.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "ir_runtime.h"
#include "ir_bridge.h"

#ifndef IR_CALL_STATIC_DEPTH
#define IR_CALL_STATIC_DEPTH 256
#endif

static IrCallFrame IrFramePool[IR_CALL_STATIC_DEPTH];
IrCallFrame *IrCallStack[IR_CALL_MAX_DEPTH];
int IrCallDepth = 0;
size_t IrStackChargeBytes = 0;
char *IrGotoTarget = NULL;

IrStaticLocalEntry *IrStaticLocalGet(const char *Name, int Line, int Create)
{
    static IrStaticLocalEntry IrStaticLocals[256];
    static int IrStaticLocalCount = 0;
    int i;
    if (Name == NULL) return NULL;
    for (i = 0; i < IrStaticLocalCount; i++)
        if (strcmp(IrStaticLocals[i].Name, Name) == 0
            && IrStaticLocals[i].Line == Line)
            return &IrStaticLocals[i];
    if (!Create || IrStaticLocalCount >= 256)
        return NULL;
    {
        IrStaticLocalEntry *E = &IrStaticLocals[IrStaticLocalCount++];
        strncpy(E->Name, Name, sizeof(E->Name) - 1);
        E->Name[sizeof(E->Name) - 1] = 0;
        E->Line = Line;
        E->Val = 0;
        E->Inited = 0;
        return E;
    }
}

static int IrFramePtrInArena(IrCallFrame *F, void *P)
{
    char *c;
    if (F == NULL || F->Arena == NULL || P == NULL)
        return 0;
    c = (char *)P;
    return (c >= F->Arena && c < F->Arena + F->ArenaCap);
}

/* Keep the local-slot extent contract in one place. A non-positive extent
 * means that the producer has not published a trustworthy bound yet; callers
 * must then use the full fixed capacity rather than risk missing live metadata. */
void IrFrameSetLocalExtent(IrCallFrame *F, int Extent)
{
    if (F == NULL)
        return;
    if (Extent < 0) Extent = 0;
    if (Extent > IR_CALL_MAX_LOCALS) Extent = IR_CALL_MAX_LOCALS;
    F->NumLocals = Extent;
    F->LocalExtentKnown = 1;
}

void IrFrameNoteLocalSlot(IrCallFrame *F, int Slot)
{
    if (F == NULL || Slot < 0 || Slot >= IR_CALL_MAX_LOCALS)
        return;
    if (!F->LocalExtentKnown || F->NumLocals <= Slot)
        IrFrameSetLocalExtent(F, Slot + 1);
}

void IrFrameSetLocalOwned(IrCallFrame *F, int Slot, int Owned)
{
    if (F == NULL || Slot < 0 || Slot >= IR_CALL_MAX_LOCALS)
        return;
    Owned = (Owned != 0);
    if ((int)F->LocalOwned[Slot] == Owned)
        return;
    if (Owned)
        F->NumOwnedLocals++;
    else if (F->NumOwnedLocals > 0)
        F->NumOwnedLocals--;
    F->LocalOwned[Slot] = (char)Owned;
}

int IrFrameLocalExtent(const IrCallFrame *F)
{
    int n;
    if (F == NULL)
        return 0;
    if (!F->LocalExtentKnown)
        return IR_CALL_MAX_LOCALS;
    n = F->NumLocals;
    if (n < 0) return 0;
    if (n > IR_CALL_MAX_LOCALS) return IR_CALL_MAX_LOCALS;
    return n;
}

void *IrFrameArenaAlloc(IrCallFrame *F, size_t n)
{
    size_t aligned;
    if (F == NULL || n == 0)
        return NULL;
    aligned = (n + 7u) & ~(size_t)7;
    if (F->Arena == NULL)
    {
        unsigned cap = (aligned < 8192) ? 8192u : (unsigned)aligned;
        F->Arena = (char *)malloc(cap);
        if (F->Arena == NULL)
            return NULL;
        F->ArenaCap = cap;
        F->ArenaUsed = 0;
    }
    if ((size_t)F->ArenaUsed + aligned > (size_t)F->ArenaCap)
        return NULL;
    {
        void *p = F->Arena + F->ArenaUsed;
        F->ArenaUsed += (unsigned)aligned;
        return p;
    }
}

void IrFrameFreeOwned(IrCallFrame *F)
{
    int li;
    if (F == NULL) return;
    for (li = 0; li < F->NumLocals && li < IR_CALL_MAX_LOCALS; li++)
    {
        if (F->LocalIsStatic[li] && F->LocalName[li] != NULL
            && !F->LocalOwned[li] && F->LocalArraySize[li] == 0)
        {
            IrStaticLocalEntry *E = IrStaticLocalGet(F->LocalName[li], F->LocalStaticLine[li], 1);
            if (E != NULL)
            {
                E->Val = F->LocalVal[li];
                E->Inited = 1;
            }
        }
        if (F->LocalStackCharge[li] > 0)
        {
            if (IrStackChargeBytes >= F->LocalStackCharge[li])
                IrStackChargeBytes -= F->LocalStackCharge[li];
            else
                IrStackChargeBytes = 0;
            F->LocalStackCharge[li] = 0;
        }
        if (F->LocalOwned[li] && F->LocalVal[li] != 0 && !F->LocalIsStatic[li])
        {
            void *p = (void *)(intptr_t)F->LocalVal[li];
            if (!IrFramePtrInArena(F, p))
                free(p);
            F->LocalVal[li] = 0;
            IrFrameSetLocalOwned(F, li, 0);
        }
        if (F->LocalInitMap[li] != NULL)
        {
            if (!IrFramePtrInArena(F, F->LocalInitMap[li]))
                free(F->LocalInitMap[li]);
            F->LocalInitMap[li] = NULL;
            F->LocalArraySize[li] = 0;
            F->LocalNDims[li] = 0;
        }
    }
    if (F->Arena != NULL)
    {
        free(F->Arena);
        F->Arena = NULL;
        F->ArenaUsed = 0;
        F->ArenaCap = 0;
    }
}


/* Mark a scalar local initialized when an opaque library routine writes via
 * its address (notably scanf-family variadic destinations).  Address-taken
 * scalar locals use the address of their LocalVal slot in both IR engines. */
void IrFrameMarkAddressInitialized(struct ParseState *Parser, void *Addr, int Size)
{
    int d;
    uintptr_t Target;
    if (Addr == NULL || Size <= 0) return;
    Target = (uintptr_t)Addr;
    for (d = IrCallDepth - 1; d >= 0; --d) {
        IrCallFrame *F = IrCallStack[d];
        uintptr_t Lo, Hi, Delta;
        int i;
        if (F == NULL) continue;
        /* An address-taken local can be passed to scanf before ordinary IR
         * reads have caused its slot to be noted.  Search the full physical
         * frame interval, not NumLocals (which is only a metadata high-water
         * mark), then publish the discovered slot before updating its state. */
        Lo = (uintptr_t)(void *)&F->LocalVal[0];
        Hi = (uintptr_t)(void *)&F->LocalVal[IR_CALL_MAX_LOCALS];
        if (Target < Lo || Target >= Hi) continue;
        Delta = Target - Lo;
        if (Delta % sizeof(F->LocalVal[0]) != 0) return;
        i = (int)(Delta / sizeof(F->LocalVal[0]));
        if (i < 0 || i >= IR_CALL_MAX_LOCALS) return;
        IrFrameNoteLocalSlot(F, i);
        F->LocalInit[i] = 1;
        if (F->LocalName[i] != NULL)
            IrShadowMarkAddrEx(Parser, F->LocalName[i], Addr, Size, 1);
        return;
    }
}

/* Pool frames (IrFramePool) are BSS-zero at process start. After each pop we
 * clear only the slots that were used (NumLocals), so the next push only needs
 * a tiny header reset — avoids the 10 KB memset on every call that dominated
 * recursive microbench (fibc/tri). Heap frames (depth >= STATIC) still get a
 * full memset once at malloc. */
/* 1b general: after a CompactClear pop the pool slot only needs a light
 * header reset — caller rewrites ParamVal[0..NumParams). Skipping the
 * 16-wide ParamName/Typ wipe saves work on every nested/recursive call. */
static void IrFrameClearHeader(IrCallFrame *F)
{
    int i;
    int was_compact = F->CompactClear;
    F->NumParams = 0;
    F->CallAbiArguments = NULL;
    F->CallAbiArgumentCount = 0;
    F->CallAbiFixedArgumentCount = 0;
    F->CallAbiIsVariadic = 0;
    F->NumLocals = 0;
    F->NumOwnedLocals = 0;
    F->LocalExtentKnown = 0;
    F->ReturnVal = 0;
    F->HasReturn = 0;
    F->Arena = NULL;
    F->ArenaUsed = 0;
    F->ArenaCap = 0;
    F->CompactClear = 0;
    if (was_compact)
    {
        for (i = 0; i < IR_CALL_MAX_PARAMS; i++)
            F->ParamVal[i] = 0;
        return;
    }
    for (i = 0; i < IR_CALL_MAX_PARAMS; i++)
    {
        F->ParamName[i] = NULL;
        F->ParamVal[i] = 0;
        F->ParamIsPtr[i] = 0;
        F->ParamTyp[i] = NULL;
    }
}

/* Reset local-slot metadata for indices [0, n) after FreeOwned.
 * Skip LocalVal/LocalDims: unread when LocalInit==0 and ArraySize/NDims==0.
 * FreeOwned already nulled Owned/InitMap and freed payloads. */
/* Full clear: all meta (array / name / type / static). */
static void IrFrameClearLocalSlotsFull(IrCallFrame *F, int n)
{
    int i;
    if (n <= 0)
        return;
    if (n > IR_CALL_MAX_LOCALS)
        n = IR_CALL_MAX_LOCALS;
    for (i = 0; i < n; i++)
    {
        F->LocalVal[i] = 0;
        F->LocalInit[i] = 0;
        F->LocalIsStatic[i] = 0;
        F->LocalStaticLine[i] = 0;
        F->LocalIsConst[i] = 0;
        F->LocalName[i] = NULL;
        F->LocalIsPtr[i] = 0;
        F->LocalIsFptr[i] = 0;
        F->LocalElemIsPtr[i] = 0;
        IrFrameSetLocalOwned(F, i, 0);
        F->LocalInitMap[i] = NULL;
        F->LocalElemSize[i] = 0;
        F->LocalPointeeSize[i] = 0;
        F->LocalFromType[i] = NULL;
        F->LocalTyp[i] = NULL;
        F->LocalArraySize[i] = 0;
        F->LocalNDims[i] = 0;
        F->LocalStackCharge[i] = 0;
    }
}

/* 1b hot clear for TplReady scalar frames: only fields touched by leaf/int/ptr
 * path. Cold fields (Name/Static/Dims/Typ/FromType/Array) stay 0 from prior
 * full clear or BSS — CompactClear frames never write them. */
static void IrFrameClearLocalSlotsHot(IrCallFrame *F, int n)
{
    int i;
    if (n <= 0)
        return;
    if (n > IR_CALL_MAX_LOCALS)
        n = IR_CALL_MAX_LOCALS;
    /* Contiguous LocalVal wipe — one store stream vs per-slot stores. */
    memset(F->LocalVal, 0, (size_t)n * sizeof(F->LocalVal[0]));
    for (i = 0; i < n; i++)
    {
        F->LocalInit[i] = 0;
        F->LocalIsPtr[i] = 0;
        IrFrameSetLocalOwned(F, i, 0);
        F->LocalElemSize[i] = 0;
    }
}

static void IrFrameClearLocalSlots(IrCallFrame *F, int n)
{
    if (F != NULL && F->CompactClear)
    {
        IrFrameClearLocalSlotsHot(F, n);
        /* Keep CompactClear so the next push uses the light header path.
         * Any path that writes cold meta (array/name/typ) clears the flag. */
    }
    else
    {
        IrFrameClearLocalSlotsFull(F, n);
        if (F != NULL)
            F->CompactClear = 0;
    }
}

IrCallFrame *IrFramePush(struct ParseState *Parser)
{
    IrCallFrame *F;
    int idx;
    if (IrCallDepth >= IR_CALL_MAX_DEPTH)
    {
        if (Parser != NULL)
            ProgramFail(Parser, "IR call stack overflow");
        return NULL;
    }
    idx = IrCallDepth++;
    IrLowerOnFramePush();
    if (idx < IR_CALL_STATIC_DEPTH)
    {
        F = &IrFramePool[idx];
        /* Pool is clean: BSS-zero first time, ClearLocalSlots on every prior pop. */
        IrFrameClearHeader(F);
    }
    else
    {
        F = (IrCallFrame *)malloc(sizeof(IrCallFrame));
        if (F == NULL)
        {
            IrCallDepth--;
            IrLowerOnFramePop();
            if (Parser != NULL)
                ProgramFail(Parser, "out of memory (IR call frame)");
            return NULL;
        }
        memset(F, 0, sizeof(*F));
    }
    IrCallStack[idx] = F;
    return F;
}

void IrFramePop(void)
{
    int idx;
    IrCallFrame *F;
    if (IrCallDepth <= 0)
        return;
    idx = IrCallDepth - 1;
    F = IrCallStack[idx];
    if (F != NULL)
    {
        int used = F->NumLocals;
        IrFrameFreeOwned(F);
        /* Return pool slot to a known-clean state for the next push. */
        if (idx < IR_CALL_STATIC_DEPTH)
            IrFrameClearLocalSlots(F, used);
    }
    if (idx >= IR_CALL_STATIC_DEPTH && F != NULL)
        free(F);
    IrCallStack[idx] = NULL;
    IrCallDepth--;
    IrLowerOnFramePop();
}

IrCallFrame *IrTokenCallSaveFrames(int Depth)
{
    IrCallFrame *Copy;
    int i;
    if (Depth <= 0)
        return NULL;
    Copy = (IrCallFrame *)malloc(sizeof(IrCallFrame) * (size_t)Depth);
    if (Copy == NULL)
        return NULL;
    for (i = 0; i < Depth; i++)
    {
        if (IrCallStack[i] != NULL)
            Copy[i] = *IrCallStack[i];
        else
            memset(&Copy[i], 0, sizeof(Copy[i]));
    }
    return Copy;
}

void IrTokenCallRestoreFrames(IrCallFrame *Copy, int Depth)
{
    int i;
    if (Copy == NULL)
        return;
    for (i = 0; i < Depth; i++)
    {
        if (IrCallStack[i] == NULL)
        {
            if (i < IR_CALL_STATIC_DEPTH)
                IrCallStack[i] = &IrFramePool[i];
            else
                continue;
        }
        if (IrCallStack[i] != NULL)
            *IrCallStack[i] = Copy[i];
    }
    free(Copy);
}

/* Name lookup on current IR frame (newest-first shadowing) */
int IrFrameGetByName(const char *Name, long long *Out)
{
    int i;
    IrCallFrame *F;
    if (Name == NULL || IrCallDepth <= 0)
        return 0;
    F = IrCallStack[IrCallDepth - 1];
    /* newest-first so shadowing matches IrBindIdentSlots */
    for (i = F->NumLocals - 1; i >= 0; i--)
        if (i < IR_CALL_MAX_LOCALS && F->LocalName[i] != NULL
            && strcmp(F->LocalName[i], Name) == 0)
        {
            if (!F->LocalInit[i])
                return -1;
            *Out = F->LocalVal[i];
            return 1;
        }
    for (i = F->NumParams - 1; i >= 0; i--)
        if (F->ParamName[i] != NULL && strcmp(F->ParamName[i], Name) == 0)
        {
            *Out = F->ParamVal[i];
            return 1;
        }
    return 0;
}

int IrFrameSetByName(const char *Name, long long V)
{
    int i;
    IrCallFrame *F;
    if (Name == NULL || IrCallDepth <= 0)
        return 0;
    F = IrCallStack[IrCallDepth - 1];
    for (i = F->NumLocals - 1; i >= 0; i--)
        if (i < IR_CALL_MAX_LOCALS && F->LocalName[i] != NULL
            && strcmp(F->LocalName[i], Name) == 0)
        {
            F->LocalVal[i] = V;
            F->LocalInit[i] = 1;
            return 1;
        }
    for (i = F->NumParams - 1; i >= 0; i--)
        if (F->ParamName[i] != NULL && strcmp(F->ParamName[i], Name) == 0)
        {
            F->ParamVal[i] = V;
            return 1;
        }
    return 0;
}

