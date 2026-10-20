#include "guest_thread.h"
#include "../picoc.h"
#include "../ir_runtime.h"
#include <string.h>
#include <stdint.h>

#ifdef WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

static Picoc *BabycGuestPC;

#ifdef WIN32
static CRITICAL_SECTION BabycGuestGIL;
static int BabycGuestGILReady;
static DWORD BabycGuestGILOwner;
static volatile int BabycGuestGILHeld;
void BabycGuestGIL_Lock(void)
{
    if (!BabycGuestGILReady) {
        InitializeCriticalSection(&BabycGuestGIL);
        BabycGuestGILReady = 1;
    }
    EnterCriticalSection(&BabycGuestGIL);
    BabycGuestGILOwner = GetCurrentThreadId();
    BabycGuestGILHeld = 1;
}
void BabycGuestGIL_Unlock(void)
{
    BabycGuestGILHeld = 0;
    LeaveCriticalSection(&BabycGuestGIL);
}
int BabycGuestGIL_UnlockIfHeld(void)
{
    if (!BabycGuestGILHeld || BabycGuestGILOwner != GetCurrentThreadId())
        return 0;
    BabycGuestGILHeld = 0;
    LeaveCriticalSection(&BabycGuestGIL);
    return 1;
}
void BabycGuestGIL_RelockIf(int was_held)
{
    if (was_held)
        BabycGuestGIL_Lock();
}
#else
static pthread_mutex_t BabycGuestGIL = PTHREAD_MUTEX_INITIALIZER;
static pthread_t BabycGuestGILOwner;
static volatile int BabycGuestGILHeld;
void BabycGuestGIL_Lock(void)
{
    (void)pthread_mutex_lock(&BabycGuestGIL);
    BabycGuestGILOwner = pthread_self();
    BabycGuestGILHeld = 1;
}
void BabycGuestGIL_Unlock(void)
{
    BabycGuestGILHeld = 0;
    (void)pthread_mutex_unlock(&BabycGuestGIL);
}
int BabycGuestGIL_UnlockIfHeld(void)
{
    if (!BabycGuestGILHeld || !pthread_equal(BabycGuestGILOwner, pthread_self()))
        return 0;
    BabycGuestGILHeld = 0;
    (void)pthread_mutex_unlock(&BabycGuestGIL);
    return 1;
}
void BabycGuestGIL_RelockIf(int was_held)
{
    if (was_held)
        BabycGuestGIL_Lock();
}
#endif

void BabycGuestSetPC(Picoc *pc)
{
    BabycGuestPC = pc;
}

Picoc *BabycGuestGetPC(void)
{
    return BabycGuestPC;
}

struct Value *BabycGuestResolveFuncValue(struct Value *StartParam)
{
    struct Value *fv;
    if (StartParam == NULL || StartParam->Val == NULL)
        return NULL;
    if (StartParam->Typ != NULL && StartParam->Typ->Base == TypeFunction)
        return StartParam;
    if (StartParam->Typ != NULL && StartParam->Typ->Base == TypePointer
        && StartParam->Val->Pointer != NULL) {
        fv = (struct Value *)StartParam->Val->Pointer;
        if (fv != NULL && fv->Typ != NULL && fv->Typ->Base == TypeFunction)
            return fv;
        if (fv != NULL && fv->Typ != NULL && fv->Typ->Base == TypePointer
            && fv->Typ->FromType != NULL && fv->Typ->FromType->Base == TypeFunction
            && fv->Val != NULL && fv->Val->Pointer != NULL)
            return (struct Value *)fv->Val->Pointer;
    }
    if (StartParam->Val->Integer != 0) {
        fv = (struct Value *)(uintptr_t)(unsigned long)StartParam->Val->Integer;
        if (fv != NULL && fv->Typ != NULL && fv->Typ->Base == TypeFunction)
            return fv;
    }
    if (StartParam->Val->LongInteger != 0) {
        fv = (struct Value *)(uintptr_t)(unsigned long)StartParam->Val->LongInteger;
        if (fv != NULL && fv->Typ != NULL && fv->Typ->Base == TypeFunction)
            return fv;
    }
    return NULL;
}

void *BabycGuestCall(struct Value *FuncValue, void *arg)
{
    Picoc *pc = BabycGuestPC;
    struct ParseState Parser;
    struct Value *ParamArray[2];
    struct Value ArgVal;
    struct Value RetVal;
    union AnyValue ArgAny;
    union AnyValue RetAny;
    int nparams;
    int count;
    int oldScope;
    char *saved_heap_stack_top;
    void *saved_stack_frame;
    struct StackFrame *saved_top_stack_frame;
    int saved_ir_call_depth;
    int saved_lin_frame_depth;
    IrLinThreadContext saved_lin_context;
    void *result = NULL;

    if (pc == NULL || FuncValue == NULL || FuncValue->Val == NULL)
        return NULL;
    if (FuncValue->Val->FuncDef.Body.Pos == NULL
        && FuncValue->Val->FuncDef.BodyIr == NULL
        && FuncValue->Val->FuncDef.Intrinsic == NULL)
        return NULL;

    memset(&Parser, 0, sizeof(Parser));
    LexInitParser(&Parser, pc,
        FuncValue->Val->FuncDef.Body.SourceText
            ? FuncValue->Val->FuncDef.Body.SourceText : "",
        (void *)FuncValue->Val->FuncDef.Body.Pos,
        FuncValue->Val->FuncDef.Body.FileName
            ? FuncValue->Val->FuncDef.Body.FileName : (char *)"guest_thread",
        TRUE, FALSE);
    Parser.Mode = RunModeRun;

    memset(&ArgAny, 0, sizeof(ArgAny));
    ArgAny.Pointer = arg;
    memset(&ArgVal, 0, sizeof(ArgVal));
    ArgVal.Typ = pc->VoidPtrType ? pc->VoidPtrType : pc->CharPtrType;
    ArgVal.Val = &ArgAny;
    ArgVal.IsLValue = FALSE;
    ArgVal.IsInitialized = TRUE;

    memset(&RetAny, 0, sizeof(RetAny));
    memset(&RetVal, 0, sizeof(RetVal));
    RetVal.Typ = FuncValue->Val->FuncDef.ReturnType
        ? FuncValue->Val->FuncDef.ReturnType : &pc->VoidType;
    RetVal.Val = &RetAny;

    nparams = FuncValue->Val->FuncDef.NumParams;
    ParamArray[0] = &ArgVal;
    ParamArray[1] = NULL;

    /* Save interpreter ownership before arming the exit point. A guest
     * exit() longjmps over the normal frame epilogue, so restore stack/IR
     * state owned by this callback before returning to the native thread. */
    saved_heap_stack_top = pc->HeapStackTop;
    saved_stack_frame = pc->StackFrame;
    saved_top_stack_frame = pc->TopStackFrame;
    saved_ir_call_depth = IrCallDepth;
    saved_lin_frame_depth = LinFrameDepth();
    IrLinThreadContextCapture(&saved_lin_context);
    if (PicocPlatformSetExitPoint(pc) != 0) {
        struct ValueProxyEntry **link;
        /* exit() longjmps over FnCodeRun's normal return epilogue. Unwind
         * its linear frame too, before restoring the enclosing IR frames. */
        LinFrameRestoreDepth(saved_lin_frame_depth);
        while (IrCallDepth > saved_ir_call_depth)
            IrFramePop();
        link = &pc->ValueProxyList;
        while (*link != NULL) {
            struct ValueProxyEntry *entry = *link;
            if ((char *)entry->Owner >= saved_heap_stack_top) {
                *link = entry->Next;
                HeapFreeMem(pc, entry);
            } else
                link = &entry->Next;
        }
        pc->HeapStackTop = saved_heap_stack_top;
        pc->StackFrame = saved_stack_frame;
        pc->TopStackFrame = saved_top_stack_frame;
        IrLinThreadContextRestore(&saved_lin_context);
        return NULL;
    }

    if (FuncValue->Val->FuncDef.Intrinsic != NULL) {
        PicocCallIntrinsic(&Parser, FuncValue->Val->FuncDef.Intrinsic,
            &RetVal, ParamArray, nparams > 0 ? 1 : 0, "guest_start");
        return RetVal.Val->Pointer;
    }

    oldScope = Parser.ScopeID;
    VariableStackFrameAdd(&Parser, "guest_start", 0);
    if (pc->TopStackFrame != NULL) {
        pc->TopStackFrame->NumParams = nparams > 0 ? 1 : 0;
        pc->TopStackFrame->ReturnValue = &RetVal;
    }
    for (count = 0; count < nparams; count++) {
        struct Value *src = (count == 0) ? &ArgVal : NULL;
        if (src != NULL && FuncValue->Val->FuncDef.ParamName != NULL
            && FuncValue->Val->FuncDef.ParamName[count] != NULL)
            VariableDefine(pc, &Parser, FuncValue->Val->FuncDef.ParamName[count],
                src, NULL, TRUE);
    }
    Parser.ScopeID = oldScope + 1;
    Parser.Mode = RunModeRun;

    if (FuncValue->Val->FuncDef.BodyIr != NULL) {
        IrExecuteFunctionBody(&Parser, FuncValue, ParamArray, nparams > 0 ? 1 : 0);
    } else if (FuncValue->Val->FuncDef.Body.Pos != NULL) {
        struct ParseState FuncParser;
        ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
        FuncParser.Mode = RunModeRun;
        if (ParseStatement(&FuncParser, TRUE) != ParseResultOk) {
            VariableStackFramePop(&Parser);
            return NULL;
        }
        Parser.Mode = FuncParser.Mode;
    }
    VariableStackFramePop(&Parser);
    if (RetVal.Typ != NULL && RetVal.Typ->Base == TypePointer)
        result = RetVal.Val->Pointer;
    else if (RetVal.Typ != NULL && RetVal.Typ->Base >= TypeInt
             && RetVal.Typ->Base <= TypeUnsignedLongLong)
        result = (void *)(uintptr_t)(unsigned long)RetVal.Val->Integer;
    return result;
}

int BabycGuestCallInt2(struct Value *FuncValue, void *a, void *b)
{
    Picoc *pc = BabycGuestPC;
    struct ParseState Parser;
    struct Value *ParamArray[3];
    struct Value ArgA, ArgB;
    struct Value RetVal;
    union AnyValue AnyA, AnyB, RetAny;
    int nparams;
    int count;
    int oldScope;
    int result = 0;
    struct ValueType *PtrTyp;

    if (pc == NULL || FuncValue == NULL || FuncValue->Val == NULL)
        return 0;
    if (FuncValue->Val->FuncDef.Body.Pos == NULL
        && FuncValue->Val->FuncDef.BodyIr == NULL
        && FuncValue->Val->FuncDef.Intrinsic == NULL)
        return 0;

    PtrTyp = pc->VoidPtrType ? pc->VoidPtrType : pc->CharPtrType;

    memset(&Parser, 0, sizeof(Parser));
    LexInitParser(&Parser, pc,
        FuncValue->Val->FuncDef.Body.SourceText
            ? FuncValue->Val->FuncDef.Body.SourceText : "",
        (void *)FuncValue->Val->FuncDef.Body.Pos,
        FuncValue->Val->FuncDef.Body.FileName
            ? FuncValue->Val->FuncDef.Body.FileName : (char *)"guest_compar",
        TRUE, FALSE);
    Parser.Mode = RunModeRun;

    memset(&AnyA, 0, sizeof(AnyA));
    AnyA.Pointer = a;
    memset(&ArgA, 0, sizeof(ArgA));
    ArgA.Typ = PtrTyp;
    ArgA.Val = &AnyA;
    ArgA.IsLValue = FALSE;
    ArgA.IsInitialized = TRUE;

    memset(&AnyB, 0, sizeof(AnyB));
    AnyB.Pointer = b;
    memset(&ArgB, 0, sizeof(ArgB));
    ArgB.Typ = PtrTyp;
    ArgB.Val = &AnyB;
    ArgB.IsLValue = FALSE;
    ArgB.IsInitialized = TRUE;

    memset(&RetAny, 0, sizeof(RetAny));
    memset(&RetVal, 0, sizeof(RetVal));
    RetVal.Typ = FuncValue->Val->FuncDef.ReturnType
        ? FuncValue->Val->FuncDef.ReturnType : &pc->IntType;
    RetVal.Val = &RetAny;

    nparams = FuncValue->Val->FuncDef.NumParams;
    ParamArray[0] = &ArgA;
    ParamArray[1] = &ArgB;
    ParamArray[2] = NULL;

    if (PicocPlatformSetExitPoint(pc) != 0)
        return 0;

    if (FuncValue->Val->FuncDef.Intrinsic != NULL) {
        PicocCallIntrinsic(&Parser, FuncValue->Val->FuncDef.Intrinsic,
            &RetVal, ParamArray, nparams >= 2 ? 2 : nparams, "guest_compar");
        return RetVal.Val->Integer;
    }

    oldScope = Parser.ScopeID;
    VariableStackFrameAdd(&Parser, "guest_compar", 0);
    if (pc->TopStackFrame != NULL) {
        pc->TopStackFrame->NumParams = nparams >= 2 ? 2 : nparams;
        pc->TopStackFrame->ReturnValue = &RetVal;
    }
    for (count = 0; count < nparams && count < 2; count++) {
        struct Value *src = (count == 0) ? &ArgA : &ArgB;
        if (FuncValue->Val->FuncDef.ParamName != NULL
            && FuncValue->Val->FuncDef.ParamName[count] != NULL)
            VariableDefine(pc, &Parser, FuncValue->Val->FuncDef.ParamName[count],
                src, NULL, TRUE);
    }
    Parser.ScopeID = oldScope + 1;
    Parser.Mode = RunModeRun;

    if (FuncValue->Val->FuncDef.BodyIr != NULL) {
        IrExecuteFunctionBody(&Parser, FuncValue, ParamArray,
            nparams >= 2 ? 2 : nparams);
    } else if (FuncValue->Val->FuncDef.Body.Pos != NULL) {
        struct ParseState FuncParser;
        ParserCopy(&FuncParser, &FuncValue->Val->FuncDef.Body);
        FuncParser.Mode = RunModeRun;
        if (ParseStatement(&FuncParser, TRUE) != ParseResultOk) {
            VariableStackFramePop(&Parser);
            return 0;
        }
        Parser.Mode = FuncParser.Mode;
    }
    VariableStackFramePop(&Parser);

    if (RetVal.Typ != NULL && RetVal.Typ->Base >= TypeInt
        && RetVal.Typ->Base <= TypeUnsignedLongLong)
        result = RetVal.Val->Integer;
    else if (RetVal.Typ != NULL && RetVal.Typ->Base == TypeLongLong)
        result = (int)RetVal.Val->LongLongInteger;
    return result;
}
