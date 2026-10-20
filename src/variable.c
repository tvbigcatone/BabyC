/* picoc variable storage. This provides ways of defining and accessing
 * variables */
 
#include "interpreter.h"
#include "ir.h"
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

/* maximum size of a value to temporarily copy while we create a variable */
#define MAX_TMP_COPY_BUF 256


/* initialise the variable system */
void VariableInit(Picoc *pc)
{
    TableInitTable(&(pc->GlobalTable), &(pc->GlobalHashTable)[0], GLOBAL_TABLE_SIZE, TRUE);
    TableInitTable(&pc->StringLiteralTable, &pc->StringLiteralHashTable[0], STRING_LITERAL_TABLE_SIZE, TRUE);
    pc->TopStackFrame = NULL;
}

/* deallocate the contents of a variable */
void VariableFree(Picoc *pc, struct Value *Val)
{
    if (Val->ValOnHeap || Val->AnyValOnHeap)
    {
        /* free function bodies */
        if (Val->Typ == &pc->FunctionType && Val->Val->FuncDef.Intrinsic == NULL)
        {
            if (Val->Val->FuncDef.Body.Pos != NULL)
                HeapFreeMem(pc, (void *)Val->Val->FuncDef.Body.Pos);
            if (Val->Val->FuncDef.BodyIr != NULL)
            {
                IrFreeFunctionBody(Val->Val->FuncDef.BodyIr);
                Val->Val->FuncDef.BodyIr = NULL;
            }
        }

        /* free macro bodies */
        if (Val->Typ == &pc->MacroType)
            HeapFreeMem(pc, (void *)Val->Val->MacroDef.Body.Pos);

        /* free the AnyValue */
        if (Val->AnyValOnHeap)
            HeapFreeMem(pc, Val->Val);
    }

    if (Val->ElemInit != NULL)
    {
        HeapFreeMem(pc, Val->ElemInit);
        Val->ElemInit = NULL;
    }

    /* free the value */
    if (Val->ValOnHeap)
        HeapFreeMem(pc, Val);
}

/* deallocate the global table and the string literal table */
void VariableTableCleanup(Picoc *pc, struct Table *HashTable)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;
    
    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            VariableFree(pc, Entry->p.v.Val);
                
            /* free the hash table entry */
            HeapFreeMem(pc, Entry);
        }
    }
}

void VariableCleanup(Picoc *pc)
{
    /* ProgramFail() exits through the host exit point with longjmp.  If that
     * happens while interpreting a function, normal scope/frame pops are
     * bypassed and heap-backed locals (notably resized arrays) are no longer
     * reachable from the global table.  Drain active local tables before the
     * heap is torn down; their stack-resident Value descriptors remain owned
     * by HeapMemory and are reclaimed with the stack arena. */
    {
        struct StackFrame *Frame;
        for (Frame = pc->TopStackFrame; Frame != NULL;
             Frame = Frame->PreviousStackFrame)
        {
            int Bucket;
            for (Bucket = 0; Bucket < Frame->LocalTable.Size; Bucket++)
            {
                struct TableEntry *Entry;
                for (Entry = Frame->LocalTable.HashTable[Bucket];
                     Entry != NULL; Entry = Entry->Next)
                    VariableFree(pc, Entry->p.v.Val);
            }
        }
        pc->TopStackFrame = NULL;
    }

    VariableTableCleanup(pc, &pc->GlobalTable);
    VariableTableCleanup(pc, &pc->StringLiteralTable);
}

/* allocate some memory, either on the heap or the stack and check if we've run out */
void *VariableAlloc(Picoc *pc, struct ParseState *Parser, int Size, int OnHeap)
{
    void *NewValue;
    
    if (OnHeap)
        NewValue = HeapAllocMem(pc, Size);
    else
        NewValue = HeapAllocStack(pc, Size);
    
    if (NewValue == NULL)
    {
        if (OnHeap)
            ProgramFail(Parser, DiagnosticMsgOutOfMemory);
        else
            ProgramFail(Parser, "stack overflow (local variables / call frames exceed stack size)");
    }
    
#ifdef DEBUG_HEAP
    if (!OnHeap)
        printf("pushing %d at 0x%lx\n", Size, (unsigned long)NewValue);
#endif
        
    return NewValue;
}

/* allocate a value either on the heap or the stack using space dependent on what type we want */
struct Value *VariableAllocValueAndData(Picoc *pc, struct ParseState *Parser, int DataSize, int IsLValue, struct Value *LValueFrom, int OnHeap)
{
    struct Value *NewValue = VariableAlloc(pc, Parser, MEM_ALIGN(sizeof(struct Value)) + DataSize, OnHeap);
    NewValue->Val = (union AnyValue *)((char *)NewValue + MEM_ALIGN(sizeof(struct Value)));
    NewValue->ValRaw = NULL;
    NewValue->ValOnHeap = OnHeap;
    NewValue->AnyValOnHeap = FALSE;
    NewValue->ValOnStack = !OnHeap;
    NewValue->IsLValue = IsLValue;
    NewValue->LValueFrom = LValueFrom;
    NewValue->InitAliasRoot = NULL;
    /* FIX: le Value allocate on-heap a scope globale (TopStackFrame==NULL) devono
     * avere ScopeID=0 ("permanente"), altrimenti un Parser locale con ScopeID
     * garbage (es. da LibraryAdd) farebbe sì che VariableScopeEnd marcasse
     * OutOfScope funzioni di libreria come printf quando il loro ScopeID
     * coincidesse accidentalmente con uno scope del parser utente.
     * ScopeID!=0 è significativo solo per variabili locali sullo stack. */
    if (Parser && !(OnHeap && pc->TopStackFrame == NULL))
        NewValue->ScopeID = Parser->ScopeID;
    else
        NewValue->ScopeID = 0;

    NewValue->OutOfScope = 0;
    NewValue->IsInitialized = 0;
    NewValue->IsConst = 0;
    NewValue->IsVolatile = 0;
    NewValue->ElemInit = NULL;
    NewValue->ElemInitCount = 0;
    NewValue->BitOffset = 0;
    NewValue->BitWidth = 0;
    NewValue->DeclIndex = 0;
    
    return NewValue;
}

/* allocate a value given its type */
struct Value *VariableAllocValueFromType(Picoc *pc, struct ParseState *Parser, struct ValueType *Typ, int IsLValue, struct Value *LValueFrom, int OnHeap)
{
    int Size = TypeSize(Typ, Typ->ArraySize, FALSE);
    struct Value *NewValue;
#ifndef NO_FP
    /* A binary32 lvalue needs two representations: four bytes of C object
     * storage and an aligned double-sized execution slot. Keeping Val on the
     * execution slot prevents FP reads/writes from overrunning the object. */
    if (Typ != NULL && Typ->Base == TypeFP
        && Typ->FPFormat == FPStorageBinary32 && IsLValue)
    {
        int RawSize = (int)sizeof(float);
        int SlotOffset = MEM_ALIGN(RawSize);
        NewValue = VariableAllocValueAndData(pc, Parser,
            SlotOffset + (int)sizeof(union AnyValue), IsLValue, LValueFrom, OnHeap);
        assert(NewValue != NULL);
        NewValue->Typ = Typ;
        NewValue->ValRaw = (void *)NewValue->Val;
        NewValue->Val = (union AnyValue *)((char *)NewValue->Val + SlotOffset);
        /* Do not initialize the proxy here: object initialization and
         * uninitialized-read diagnostics remain owned by existing paths. */
        return NewValue;
    }
#endif
    /* A non-lvalue binary32 value is an execution temporary, not a C object. */
#ifndef NO_FP
    if (Typ != NULL && Typ->Base == TypeFP
        && Typ->FPFormat == FPStorageBinary32 && !IsLValue
        && Size < (int)sizeof(double))
        Size = (int)sizeof(double);
#endif
    NewValue = VariableAllocValueAndData(pc, Parser, Size, IsLValue, LValueFrom, OnHeap);
    assert(Size >= 0 || Typ == &pc->VoidType);
    NewValue->Typ = Typ;
    return NewValue;
}

/* allocate a value either on the heap or the stack and copy its value. handles overlapping data */
struct Value *VariableAllocValueAndCopy(Picoc *pc, struct ParseState *Parser, struct Value *FromValue, int OnHeap)
{
    struct ValueType *DType = FromValue->Typ;
    struct Value *NewValue;
    char TmpBuf[MAX_TMP_COPY_BUF];
    int CopySize = TypeSizeValue(FromValue, TRUE);
#ifndef NO_FP
    /* Binary32 objects occupy four bytes in C memory, but Value->Val is a
     * double execution slot. Copy the value, not the object's raw bytes. */
    if (DType != NULL && DType->Base == TypeFP
        && DType->FPFormat == FPStorageBinary32)
    {
        /* Re-read the object representation before copying a binary32 lvalue:
         * pointer aliases may have changed memory since its proxy was formed. */
        VariableValueRefresh(FromValue);
        NewValue = VariableAllocValueFromType(pc, Parser, DType,
            FromValue->IsLValue, FromValue->LValueFrom, OnHeap);
        NewValue->Val->FP = (double)(float)FromValue->Val->FP;
        NewValue->IsInitialized = FromValue->IsInitialized;
        NewValue->InitAliasRoot = FromValue->InitAliasRoot;
        NewValue->IsConst = FromValue->IsConst;
        NewValue->IsVolatile = FromValue->IsVolatile;
        NewValue->ElemInit = NULL;
        NewValue->ElemInitCount = 0;
        return NewValue;
    }
#endif
    assert(CopySize <= MAX_TMP_COPY_BUF);
    memcpy((void *)&TmpBuf[0], (void *)FromValue->Val, CopySize);
    NewValue = VariableAllocValueAndData(pc, Parser, CopySize, FromValue->IsLValue, FromValue->LValueFrom, OnHeap);
    NewValue->Typ = DType;
    memcpy((void *)NewValue->Val, (void *)&TmpBuf[0], CopySize);
    NewValue->IsInitialized = FromValue->IsInitialized;
    NewValue->InitAliasRoot = FromValue->InitAliasRoot;
    NewValue->IsConst = FromValue->IsConst;
    NewValue->IsVolatile = FromValue->IsVolatile;
    NewValue->ElemInit = NULL;
    NewValue->ElemInitCount = 0;
    
    return NewValue;
}

/* Refresh/commit aligned scalar proxies used for potentially misaligned lvalues.
 * memcpy is standard C and supported by GCC/Clang and MSVC; aligned values keep
 * the original zero-overhead direct path. */
void VariableValueRefresh(struct Value *Val)
{
    if (Val == NULL || Val->ValRaw == NULL || Val->Typ == NULL)
        return;
#ifndef NO_FP
    /* A binary32 lvalue is represented by a double execution temporary.
     * Decode it explicitly; copying four raw bytes into a double is invalid
     * on both endian orders and does not perform an IEEE-754 conversion. */
    if (Val->Typ->Base == TypeFP && Val->Typ->FPFormat == FPStorageBinary32)
    {
        float Stored;
        memcpy(&Stored, Val->ValRaw, sizeof(Stored));
        Val->Val->FP = (double)Stored;
        return;
    }
#endif
    if (Val->Typ->Sizeof > 0 && Val->Typ->Sizeof <= (int)sizeof(union AnyValue))
        memcpy((void *)Val->Val, Val->ValRaw, (size_t)Val->Typ->Sizeof);
}

void VariableValueCommit(struct Value *Val)
{
    if (Val == NULL || Val->ValRaw == NULL || Val->Typ == NULL)
        return;
#ifndef NO_FP
    /* Round exactly at a binary32 storage boundary, keeping the VM's working
     * value in double precision after the conversion. */
    if (Val->Typ->Base == TypeFP && Val->Typ->FPFormat == FPStorageBinary32)
    {
        float Stored = (float)Val->Val->FP;
        memcpy(Val->ValRaw, &Stored, sizeof(Stored));
        Val->Val->FP = (double)Stored;
        return;
    }
#endif
    if (Val->Typ->Sizeof > 0 && Val->Typ->Sizeof <= (int)sizeof(union AnyValue))
        memcpy(Val->ValRaw, (const void *)Val->Val, (size_t)Val->Typ->Sizeof);
}

/* allocate a value either on the heap or the stack from an existing AnyValue and type */
struct Value *VariableAllocValueFromExistingData(struct ParseState *Parser, struct ValueType *Typ, union AnyValue *FromValue, int IsLValue, struct Value *LValueFrom)
{
    int NeedProxy = FALSE;
    size_t AnyValueAlign = 1;
    struct Value *NewValue;

    /* Only scalar views can use the compact trailing proxy. Aggregate objects
     * must retain their original byte-addressable storage and layout. */
    if (FromValue != NULL && Typ != NULL && Typ->Sizeof > 0
        && Typ->Sizeof <= (int)sizeof(union AnyValue)
        && Typ->Base != TypeArray && Typ->Base != TypeStruct
        && Typ->Base != TypeUnion && Typ->Base != TypeFunction
        && Typ->Base != TypeVoid)
    {
        struct AnyValueAlignmentProbe { char c; union AnyValue value; };
        AnyValueAlign = offsetof(struct AnyValueAlignmentProbe, value);
        if (AnyValueAlign > 1 && ((size_t)(uintptr_t)FromValue % AnyValueAlign) != 0)
            NeedProxy = TRUE;
#ifndef NO_FP
        /* Even aligned binary32 storage cannot be used as a double temporary. */
        if (Typ->Base == TypeFP && Typ->FPFormat == FPStorageBinary32)
            NeedProxy = TRUE;
#endif
    }

    NewValue = VariableAlloc(Parser->pc, Parser,
        MEM_ALIGN(sizeof(struct Value)) + (NeedProxy ? (int)sizeof(union AnyValue) : 0), FALSE);
    if (NewValue == NULL)
        return NULL;
    NewValue->Typ = Typ;
    NewValue->Val = FromValue;
    NewValue->ValRaw = NULL;
    NewValue->ValOnHeap = FALSE;
    NewValue->AnyValOnHeap = FALSE;
    NewValue->ValOnStack = FALSE;

    if (NeedProxy)
    {
        union AnyValue *Aligned = (union AnyValue *)((char *)NewValue + MEM_ALIGN(sizeof(struct Value)));
        struct ValueProxyEntry *Entry;
#ifndef NO_FP
        if (Typ->Base == TypeFP && Typ->FPFormat == FPStorageBinary32)
        {
            float Stored;
            memcpy(&Stored, (const void *)FromValue, sizeof(Stored));
            Aligned->FP = (double)Stored;
        }
        else
#endif
            memcpy(Aligned, (const void *)FromValue, (size_t)Typ->Sizeof);
        NewValue->Val = Aligned;
        NewValue->ValRaw = (void *)FromValue;
        /* Register the extra trailing storage so HeapPopStack can account for
         * it without inflating every Value or changing TypeStackSizeValue. */
        Entry = (struct ValueProxyEntry *)HeapAllocMem(Parser->pc, (int)sizeof(*Entry));
        if (Entry == NULL)
            ProgramFail(Parser, "out of memory allocating scalar proxy metadata");
        Entry->Owner = NewValue;
        Entry->Next = Parser->pc->ValueProxyList;
        Parser->pc->ValueProxyList = Entry;
    }
    NewValue->ValOnStack = FALSE;
    NewValue->IsLValue = IsLValue;
    NewValue->LValueFrom = LValueFrom;
    NewValue->InitAliasRoot = (LValueFrom != NULL) ? LValueFrom->InitAliasRoot : NULL;
    NewValue->OutOfScope = 0;
    NewValue->IsInitialized = (LValueFrom != NULL) ? LValueFrom->IsInitialized : 0;
    NewValue->IsConst = 0;
    NewValue->IsVolatile = 0;
    NewValue->ElemInit = NULL;
    NewValue->ElemInitCount = 0;
    NewValue->BitOffset = 0;
    NewValue->BitWidth = 0;
    NewValue->DeclIndex = 0;
    /* do NOT set ScopeID — shared/expression values inherit or stay 0 */
    
    return NewValue;
}

/* allocate a value either on the heap or the stack from an existing Value, sharing the value */
struct Value *VariableAllocValueShared(struct ParseState *Parser, struct Value *FromValue)
{
    return VariableAllocValueFromExistingData(Parser, FromValue->Typ,
        (union AnyValue *)(FromValue->ValRaw != NULL ? FromValue->ValRaw : (void *)FromValue->Val),
        FromValue->IsLValue, FromValue->IsLValue ? FromValue : NULL);
}

/* reallocate a variable so its data has a new size */
void VariableRealloc(struct ParseState *Parser, struct Value *FromValue, int NewSize)
{
    if (FromValue->AnyValOnHeap)
        HeapFreeMem(Parser->pc, FromValue->Val);
        
    FromValue->Val = VariableAlloc(Parser->pc, Parser, NewSize, TRUE);
    FromValue->AnyValOnHeap = TRUE;
}

int VariableScopeBegin(struct ParseState * Parser, int* OldScopeID)
{
    VariableCacheInvalidate(Parser->pc);
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    Picoc * pc = Parser->pc;
    int Count;
    #ifdef VAR_SCOPE_DEBUG
    int FirstPrint = 0;
    #endif
    
    struct Table * HashTable = (pc->TopStackFrame == NULL) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;

    if (Parser->ScopeID == -1) return -1;

    /* XXX dumb hash, let's hope for no collisions... */
    *OldScopeID = Parser->ScopeID;
    // Parser->ScopeID = (int)(intptr_t)(Parser->SourceText) * ((int)(intptr_t)(Parser->Pos) / sizeof(char*));
    Parser->ScopeID = Parser->pc->NextScopeID++;

    /* or maybe a more human-readable hash for debugging? */
    /* Parser->ScopeID = Parser->Line * 0x10000 + Parser->CharacterPos; */
    
    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            if (Entry->p.v.Val->ScopeID == Parser->ScopeID && Entry->p.v.Val->OutOfScope)
            {
                Entry->p.v.Val->OutOfScope = FALSE;
                Entry->p.v.Key = (char*)((intptr_t)Entry->p.v.Key & ~1);
                #ifdef VAR_SCOPE_DEBUG
                if (!FirstPrint) { PRINT_SOURCE_POS; }
                FirstPrint = 1;
                printf(">>> back into scope: %s %x %d\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID, Entry->p.v.Val->Val->Integer);
                #endif
            }
        }
    }

    return Parser->ScopeID;
}

void VariableScopeEnd(struct ParseState * Parser, int ScopeID, int PrevScopeID)
{
    VariableCacheInvalidate(Parser->pc);
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    Picoc * pc = Parser->pc;
    int Count;
    #ifdef VAR_SCOPE_DEBUG
    int FirstPrint = 0;
    #endif

    struct Table * HashTable = (pc->TopStackFrame == NULL) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;

    /* ScopeID==-1: scope tracking disabilitato (parser di libreria/platform).
     * ScopeID==0:  scope globale permanente, non va mai chiuso. */
    if (ScopeID == -1 || ScopeID == 0) return;

    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            /* FIX: saltare esplicitamente le entry con ScopeID==0 (globali
             * permanenti): sono funzioni di libreria o variabili platform e
             * non devono mai risultare OutOfScope. */
            if (Entry->p.v.Val->ScopeID == 0)
                continue;
            if (Entry->p.v.Val->ScopeID == ScopeID && !Entry->p.v.Val->OutOfScope)
            {
                #ifdef VAR_SCOPE_DEBUG
                if (!FirstPrint) { PRINT_SOURCE_POS; }
                FirstPrint = 1;
                printf(">>> out of scope: %s %x %d\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID, Entry->p.v.Val->Val->Integer);
                #endif
                Entry->p.v.Val->OutOfScope = TRUE;
                Entry->p.v.Key = (char*)((intptr_t)Entry->p.v.Key | 1); /* alter the key so it won't be found by normal searches */
            }
        }
    }

    /* Restore variables of the enclosing scope that were temporarily hidden
     * by shadowing (see VariableDefine). Their keys were mangled while still
     * "in scope"; once the inner scope ends they must become visible again. */
    if (PrevScopeID != -1 && PrevScopeID != 0)
    {
        for (Count = 0; Count < HashTable->Size; Count++)
        {
            for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = Entry->Next)
            {
                if (Entry->p.v.Val->ScopeID == PrevScopeID
                    && ((intptr_t)Entry->p.v.Key & 1)
                    && !Entry->p.v.Val->OutOfScope)
                {
                    Entry->p.v.Key = (char *)((intptr_t)Entry->p.v.Key & ~1);
                    #ifdef VAR_SCOPE_DEBUG
                    printf(">>> unshadow: %s %x\n", Entry->p.v.Key, Entry->p.v.Val->ScopeID);
                    #endif
                }
            }
        }
    }

    Parser->ScopeID = PrevScopeID;
}

int VariableDefinedAndOutOfScope(Picoc * pc, const char* Ident)
{
    struct TableEntry *Entry;
    int Count;

    struct Table * HashTable = (pc->TopStackFrame == NULL) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;
    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = Entry->Next)
        {
            if (Entry->p.v.Val->OutOfScope && (char*)((intptr_t)Entry->p.v.Key & ~1) == Ident)
                return TRUE;
        }
    }
    return FALSE;
}

/* define a variable. Ident must be registered */
struct Value *VariableDefine(Picoc *pc, struct ParseState *Parser, char *Ident, struct Value *InitValue, struct ValueType *Typ, int MakeWritable)
{
    VariableCacheInvalidate(pc);
    struct Value * AssignValue;
    struct Table * currentTable = (pc->TopStackFrame == NULL) ? &(pc->GlobalTable) : &(pc->TopStackFrame)->LocalTable;
    
    int ScopeID = Parser ? Parser->ScopeID : -1;
#ifdef VAR_SCOPE_DEBUG
    if (Parser) fprintf(stderr, "def %s %x (%s:%d:%d)\n", Ident, ScopeID, Parser->FileName, Parser->Line, Parser->CharacterPos);
#endif
    
    if (InitValue != NULL)
        AssignValue = VariableAllocValueAndCopy(pc, Parser, InitValue, pc->TopStackFrame == NULL);
    else
        AssignValue = VariableAllocValueFromType(pc, Parser, Typ, MakeWritable, NULL, pc->TopStackFrame == NULL);
    
    AssignValue->IsLValue = MakeWritable;
    AssignValue->ScopeID = ScopeID;
    AssignValue->OutOfScope = FALSE;
    /* Memory: always zero the object at declaration (scalar, array, struct).
     * Semantics: static/global are defined (C zero-init). Automatic storage
     * is still "not assigned" — use without assignment is diagnosed. */
    if (InitValue == NULL && AssignValue->Val != NULL)
    {
        int Z = TypeSizeValue(AssignValue, FALSE);
        if (Z > 0)
            memset(AssignValue->Val, 0, Z);
    }
    if (InitValue != NULL)
        AssignValue->IsInitialized = TRUE;
    else if (pc->TopStackFrame == NULL)
        AssignValue->IsInitialized = TRUE;
    else
        AssignValue->IsInitialized = FALSE;

    /* Automatic array/struct without initializer: per-byte init bitmap, so that
     * a[i], m[i][j], a[i].m, s.m and s.arr[i] are each tracked on their own. */
    AssignValue->ElemInit = NULL;
    AssignValue->ElemInitCount = 0;
    if (Typ != NULL && InitValue == NULL && pc->TopStackFrame != NULL
        && TypeInitTrackable(Typ))
    {
        int Bytes = TypeSizeValue(AssignValue, TRUE);
        if (Bytes > 0)
        {
            AssignValue->ElemInit = (unsigned char *)calloc((size_t)((Bytes + 7) / 8), 1);
            if (AssignValue->ElemInit != NULL)
                AssignValue->ElemInitCount = Bytes;
        }
    }

    if (!TableSet(pc, currentTable, Ident, AssignValue, Parser ? ((char *)Parser->FileName) : NULL, Parser ? Parser->Line : 0, Parser ? Parser->CharacterPos : 0))
    {
        /* Name already present in this table. In standard C a declaration in an
         * inner block is allowed to shadow an outer one (different ScopeID).
         * Same-scope redefinition remains an error. */
        struct TableEntry *Entry;
        struct TableEntry *FoundEntry = NULL;
        int HashValue = ((PTR_TYPE_INT)Ident) % currentTable->Size;

        for (Entry = currentTable->HashTable[HashValue]; Entry != NULL; Entry = Entry->Next)
        {
            if (Entry->p.v.Key == Ident)
            {
                FoundEntry = Entry;
                break;
            }
        }

        if (FoundEntry != NULL && FoundEntry->p.v.Val->ScopeID != ScopeID)
        {
            /* Shadowing: hide the outer variable (same technique used by
             * VariableScopeEnd) so the new definition can take the name. */
            FoundEntry->p.v.Key = (char *)((intptr_t)FoundEntry->p.v.Key | 1);
            /* The outer value stays allocated; it will be restored when the
             * inner scope ends (see VariableScopeEnd). */
            if (!TableSet(pc, currentTable, Ident, AssignValue,
                          Parser ? ((char *)Parser->FileName) : NULL,
                          Parser ? Parser->Line : 0,
                          Parser ? Parser->CharacterPos : 0))
                ProgramFail(Parser, "'%s' is already defined", Ident);
        }
        else
            ProgramFail(Parser, "'%s' is already defined", Ident);
    }
    
    return AssignValue;
}

/* B.3 bridge: bind token-body function params into Variable table.
 * Body-IR params use ParamVal on IrCallFrame and skip this. */
void VariableBindParams(Picoc *pc, struct ParseState *Parser,
    char **ParamNames, struct Value **ParamValues, int NumParams)
{
    int i;
    if (ParamNames == NULL || ParamValues == NULL || NumParams <= 0)
        return;
    for (i = 0; i < NumParams; i++)
    {
        if (ParamNames[i] == NULL)
            continue;
        VariableDefine(pc, Parser, ParamNames[i], ParamValues[i], NULL, TRUE);
    }
}

/* define a variable. Ident must be registered. If it's a redefinition from the same declaration don't throw an error */
struct Value *VariableDefineButIgnoreIdentical(struct ParseState *Parser, char *Ident, struct ValueType *Typ, int IsStatic, int *FirstVisit)
{
    Picoc *pc = Parser->pc;
    struct Value *ExistingValue;
    const char *DeclFileName;
    int DeclLine;
    int DeclColumn;
    
    /* is the type a forward declaration? */
    if (TypeIsForwardDeclared(Parser, Typ))
        ProgramFail(Parser, "type '%t' isn't defined", Typ);

    if (IsStatic)
    {
        char MangledName[LINEBUFFER_MAX];
        char *MNPos = &MangledName[0];
        char *MNEnd = &MangledName[LINEBUFFER_MAX-1];
        const char *RegisteredMangledName;
        
        /* make the mangled static name (avoiding using sprintf() to minimise library impact) */
        memset((void *)&MangledName, '\0', sizeof(MangledName));
        *MNPos++ = '/';
        strncpy(MNPos, (char *)Parser->FileName, MNEnd - MNPos);
        MNPos += strlen(MNPos);
        
        if (pc->TopStackFrame != NULL)
        {
            /* we're inside a function */
            if (MNEnd - MNPos > 0) *MNPos++ = '/';
            strncpy(MNPos, (char *)pc->TopStackFrame->FuncName, MNEnd - MNPos);
            MNPos += strlen(MNPos);
        }
            
        if (MNEnd - MNPos > 0) *MNPos++ = '/';
        strncpy(MNPos, Ident, MNEnd - MNPos);
        MNPos += strlen(MNPos);
        /* Distinguish statics in different blocks of the same function.
         * C allows: { static int x; } { static int x; } — two objects.
         * Use source location (line/column), NOT Parser->ScopeID:
         * ScopeID is unique per block *entry* and changes every call, so
         * using it broke static locals (always re-init, e.g. test 51_static). */
        if (MNEnd - MNPos > 2) {
            int line = Parser->Line;
            int col = Parser->CharacterPos;
            char buf[32];
            int n = 0, v, i;
            *MNPos++ = '/';
            /* append line as decimal */
            v = line < 0 ? 0 : line;
            if (v == 0) { buf[n++] = '0'; }
            else {
                while (v > 0 && n < (int)sizeof(buf) - 1) { buf[n++] = '0' + (v % 10); v /= 10; }
                for (i = 0; i < n / 2; i++) { char t = buf[i]; buf[i] = buf[n - 1 - i]; buf[n - 1 - i] = t; }
            }
            buf[n++] = '_';
            v = col < 0 ? 0 : col;
            {
                int n0 = n;
                if (v == 0) { buf[n++] = '0'; }
                else {
                    int n1 = 0; char tmp[16];
                    while (v > 0 && n1 < (int)sizeof(tmp)) { tmp[n1++] = '0' + (v % 10); v /= 10; }
                    for (i = 0; i < n1 / 2; i++) { char t = tmp[i]; tmp[i] = tmp[n1 - 1 - i]; tmp[n1 - 1 - i] = t; }
                    for (i = 0; i < n1 && n < (int)sizeof(buf) - 1; i++) buf[n++] = tmp[i];
                }
            }
            buf[n] = '\0';
            strncpy(MNPos, buf, MNEnd - MNPos);
        }
        RegisteredMangledName = TableStrRegister(pc, MangledName);
        
        /* is this static already defined? */
        if (!TableGet(&pc->GlobalTable, RegisteredMangledName, &ExistingValue, &DeclFileName, &DeclLine, &DeclColumn))
        {
            /* define the mangled-named static variable store in the global scope */
            ExistingValue = VariableAllocValueFromType(Parser->pc, Parser, Typ, TRUE, NULL, TRUE);
            {
                int Z = TypeSizeValue(ExistingValue, FALSE);
                if (Z > 0 && ExistingValue->Val != NULL)
                    memset(ExistingValue->Val, 0, Z);
            }
            ExistingValue->IsInitialized = TRUE; /* C static locals are zeroed */
            TableSet(pc, &pc->GlobalTable, (char *)RegisteredMangledName, ExistingValue, (char *)Parser->FileName, Parser->Line, Parser->CharacterPos);
            *FirstVisit = TRUE;
        }

        /* static variable exists in the global scope - now make a mirroring variable in our own scope with the short name */
        VariableDefinePlatformVar(Parser->pc, Parser, Ident, ExistingValue->Typ, ExistingValue->Val, TRUE);
        return ExistingValue;
    }
    else
    {
        struct Table *HashTable = (pc->TopStackFrame == NULL) ? &pc->GlobalTable : &pc->TopStackFrame->LocalTable;

        if (Parser->Line != 0 && TableGet(HashTable, Ident, &ExistingValue, &DeclFileName, &DeclLine, &DeclColumn)
                && DeclFileName == Parser->FileName && DeclLine == Parser->Line && DeclColumn == Parser->CharacterPos)
            return ExistingValue;

        /* Reuse an OutOfScope local declared at the same source location.
         * ScopeID changes every block entry, so VariableScopeBegin cannot
         * revive these entries; without reuse, loops like
         *   for (...) { int j = ...; }
         * allocate forever (test 60_local_vars → out of memory). */
        if (Parser->Line != 0)
        {
            int Count;
            struct TableEntry *Entry;
            for (Count = 0; Count < HashTable->Size; Count++)
            {
                for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = Entry->Next)
                {
                    char *Key = (char *)((intptr_t)Entry->p.v.Key & ~(intptr_t)1);
                    if (Entry->p.v.Val != NULL
                        && Entry->p.v.Val->OutOfScope
                        && Key == Ident
                        && Entry->DeclFileName == Parser->FileName
                        && Entry->DeclLine == Parser->Line
                        && Entry->DeclColumn == Parser->CharacterPos)
                    {
                        Entry->p.v.Key = Key; /* un-mangle search key */
                        Entry->p.v.Val->OutOfScope = FALSE;
                        Entry->p.v.Val->ScopeID = Parser->ScopeID;
                        *FirstVisit = FALSE;
                        return Entry->p.v.Val;
                    }
                }
            }
        }

        return VariableDefine(Parser->pc, Parser, Ident, NULL, Typ, TRUE);
    }
}

/* check if a variable with a given name is defined. Ident must be registered */
int VariableDefined(Picoc *pc, const char *Ident)
{
    struct Value *FoundValue;
    
    if (pc->TopStackFrame == NULL || !TableGet(&pc->TopStackFrame->LocalTable, Ident, &FoundValue, NULL, NULL, NULL))
    {
        if (!TableGet(&pc->GlobalTable, Ident, &FoundValue, NULL, NULL, NULL))
            return FALSE;
    }

    return TRUE;
}

/* get the value of a variable. must be defined. Ident must be registered */
/* Invalidate VariableGet cache — call on define, scope enter/exit, frame push/pop */
void VariableCacheInvalidate(Picoc *pc)
{
    int i;
    for (i = 0; i < VAR_CACHE_SLOTS; i++)
    {
        pc->VarCache[i].Ident = NULL;
        pc->VarCache[i].Value = NULL;
        pc->VarCache[i].Frame = NULL;
    }
    pc->VarCacheNext = 0;
}

void VariableGet(Picoc *pc, struct ParseState *Parser, const char *Ident, struct Value **LVal)
{
    int i;
    struct StackFrame *Frame = pc->TopStackFrame;

    /* Hot path: scan small cache for same Ident in current frame */
    for (i = 0; i < VAR_CACHE_SLOTS; i++)
    {
        if (pc->VarCache[i].Ident == Ident
            && pc->VarCache[i].Frame == Frame
            && pc->VarCache[i].Value != NULL
            && !pc->VarCache[i].Value->OutOfScope)
        {
            *LVal = pc->VarCache[i].Value;
            return;
        }
    }

    if (Frame == NULL || !TableGet(&Frame->LocalTable, Ident, LVal, NULL, NULL, NULL))
    {
        if (!TableGet(&pc->GlobalTable, Ident, LVal, NULL, NULL, NULL))
        {
            if (VariableDefinedAndOutOfScope(pc, Ident))
                ProgramFail(Parser, "'%s' is out of scope", Ident);
            else
            {
                ProgramFail(Parser, "'%s' is undefined", Ident);
            }
        }
    }

    /* Insert / replace (round-robin) */
    i = pc->VarCacheNext % VAR_CACHE_SLOTS;
    pc->VarCache[i].Ident = Ident;
    pc->VarCache[i].Value = *LVal;
    pc->VarCache[i].Frame = Frame;
    pc->VarCacheNext = (unsigned char)((i + 1) % VAR_CACHE_SLOTS);
}

/* define a global variable shared with a platform global. Ident will be registered */
void VariableDefinePlatformVar(Picoc *pc, struct ParseState *Parser, char *Ident, struct ValueType *Typ, union AnyValue *FromValue, int IsWritable)
{
    struct Table *currentTable = (pc->TopStackFrame == NULL) ? &pc->GlobalTable : &pc->TopStackFrame->LocalTable;
    char *Key = TableStrRegister(pc, Ident);
    struct Value *SomeValue = VariableAllocValueAndData(pc, NULL, 0, IsWritable, NULL, TRUE);
    int ScopeID = Parser ? Parser->ScopeID : 0;

    SomeValue->Typ = Typ;
    SomeValue->Val = FromValue;
    SomeValue->ScopeID = ScopeID;
    SomeValue->OutOfScope = FALSE;
    SomeValue->IsInitialized = TRUE;

    if (!TableSet(pc, currentTable, Key, SomeValue, Parser ? Parser->FileName : NULL, Parser ? Parser->Line : 0, Parser ? Parser->CharacterPos : 0))
    {
        /* Allow shadowing across different ScopeIDs (same as VariableDefine). */
        struct TableEntry *Entry;
        struct TableEntry *FoundEntry = NULL;
        int HashValue = ((PTR_TYPE_INT)Key) % currentTable->Size;

        for (Entry = currentTable->HashTable[HashValue]; Entry != NULL; Entry = Entry->Next)
        {
            if (Entry->p.v.Key == Key)
            {
                FoundEntry = Entry;
                break;
            }
        }

        if (FoundEntry != NULL && FoundEntry->p.v.Val->ScopeID != ScopeID)
        {
            FoundEntry->p.v.Key = (char *)((intptr_t)FoundEntry->p.v.Key | 1);
            if (!TableSet(pc, currentTable, Key, SomeValue, Parser ? Parser->FileName : NULL, Parser ? Parser->Line : 0, Parser ? Parser->CharacterPos : 0))
                ProgramFail(Parser, "'%s' is already defined", Ident);
        }
        else
            ProgramFail(Parser, "'%s' is already defined", Ident);
    }
}

/* free and/or pop the top value off the stack. Var must be the top value on the stack! */
void VariableStackPop(struct ParseState *Parser, struct Value *Var)
{
    int Success;
    
#ifdef DEBUG_HEAP
    if (Var->ValOnStack)
        printf("popping %ld at 0x%lx\n", (unsigned long)(sizeof(struct Value) + TypeSizeValue(Var, FALSE)), (unsigned long)Var);
#endif
        
    if (Var->ValOnHeap)
    { 
        if (Var->Val != NULL)
            HeapFreeMem(Parser->pc, Var->Val);
            
        Success = HeapPopStack(Parser->pc, Var, sizeof(struct Value));                       /* free from heap */
    }
    else if (Var->ValOnStack)
        Success = HeapPopStack(Parser->pc, Var, sizeof(struct Value) + TypeSizeValue(Var, FALSE));  /* free from stack */
    else
        Success = HeapPopStack(Parser->pc, Var, sizeof(struct Value));                       /* value isn't our problem */
        
    if (!Success)
        ProgramFail(Parser, "stack underrun");
}

/* add a stack frame when doing a function call */
void VariableStackFrameAdd(struct ParseState *Parser, const char *FuncName, int NumParams)
{
    VariableCacheInvalidate(Parser->pc);
    struct StackFrame *NewFrame;
    
    HeapPushStackFrame(Parser->pc);
    NewFrame = HeapAllocStack(Parser->pc, sizeof(struct StackFrame) + sizeof(struct Value *) * NumParams);
    if (NewFrame == NULL)
        ProgramFail(Parser, "stack overflow (too many nested calls or local variables)");
        
    ParserCopy(&NewFrame->ReturnParser, Parser);
    NewFrame->FuncName = FuncName;
    NewFrame->CallAbi.Arguments = NULL;
    NewFrame->CallAbi.ArgumentCount = 0;
    NewFrame->CallAbi.FixedArgumentCount = 0;
    NewFrame->CallAbi.IsVariadic = 0;
    NewFrame->Parameter = (NumParams > 0) ? ((void *)((char *)NewFrame + sizeof(struct StackFrame))) : NULL;
    TableInitTable(&NewFrame->LocalTable, &NewFrame->LocalHashTable[0], LOCAL_TABLE_SIZE, FALSE);
    NewFrame->PreviousStackFrame = Parser->pc->TopStackFrame;
    Parser->pc->TopStackFrame = NewFrame;
}

/* remove a stack frame */
void VariableStackFramePop(struct ParseState *Parser)
{
    VariableCacheInvalidate(Parser->pc);
    if (Parser->pc->TopStackFrame == NULL)
        ProgramFail(Parser, "stack is empty - can't go back");
        
    ParserCopy(Parser, &Parser->pc->TopStackFrame->ReturnParser);
    Parser->pc->TopStackFrame = Parser->pc->TopStackFrame->PreviousStackFrame;
    HeapPopStackFrame(Parser->pc);
}

/* get a string literal. assumes that Ident is already registered. NULL if not found */
static unsigned StrLitGen; /* bumped when string literals interned */

struct Value *VariableStringLiteralGet(Picoc *pc, char *Ident)
{
    struct Value *LVal = NULL;

    if (TableGet(&pc->StringLiteralTable, Ident, &LVal, NULL, NULL, NULL))
        return LVal;
    else
        return NULL;
}

/* define a string literal. assumes that Ident is already registered */
void VariableStringLiteralDefine(Picoc *pc, char *Ident, struct Value *Val)
{
    if (Val != NULL)
        Val->IsConst = TRUE; /* "..." lives in read-only storage */
    TableSet(pc, &pc->StringLiteralTable, Ident, Val, NULL, 0, 0);
    StrLitGen++;
}

/* Negative cache: the full table scan below is O(#literals) and runs on every
 * store through ExpressionAssign*. Same address + no new literal => still "no". */
#define STRLIT_NEG_N 4
static const void *StrLitNeg[STRLIT_NEG_N];
static unsigned StrLitNegGen[STRLIT_NEG_N];
static int StrLitNegNext;

static int VariableAddressIsStringLiteralScan(Picoc *pc, const void *Addr);
int VariableAddressIsStringLiteral(Picoc *pc, const void *Addr)
{
    int i, r;
    for (i = 0; i < STRLIT_NEG_N; i++)
        if (StrLitNeg[i] == Addr && StrLitNegGen[i] == StrLitGen)
            return 0;
    r = VariableAddressIsStringLiteralScan(pc, Addr);
    if (!r && Addr != NULL)
    {
        StrLitNeg[StrLitNegNext] = Addr;
        StrLitNegGen[StrLitNegNext] = StrLitGen;
        StrLitNegNext = (StrLitNegNext + 1) % STRLIT_NEG_N;
    }
    return r;
}

static int VariableAddressIsStringLiteralScan(Picoc *pc, const void *Addr)
{
    int Count;
    struct TableEntry *Entry;
    const char *A;

    if (pc == NULL || Addr == NULL)
        return 0;
    A = (const char *)Addr;
    /* Fast path: pointer is the start of an interned literal */
    if (VariableStringLiteralGet(pc, (char *)Addr) != NULL)
        return 1;
    /* Interior of a literal (p[i] with i>0): scan the string-literal table */
    for (Count = 0; Count < pc->StringLiteralTable.Size; Count++)
    {
        for (Entry = pc->StringLiteralTable.HashTable[Count]; Entry != NULL; Entry = Entry->Next)
        {
            const char *Key = Entry->p.v.Key;
            size_t Len;
            if (Key == NULL)
                continue;
            Len = strlen(Key) + 1; /* include NUL */
            if (A >= Key && A < Key + Len)
                return 1;
        }
    }
    return 0;
}

/* check a pointer for validity and dereference it for use */
void *VariableDereferencePointer(struct ParseState *Parser, struct Value *PointerValue, struct Value **DerefVal, int *DerefOffset, struct ValueType **DerefType, int *DerefIsLValue)
{
    if (DerefVal != NULL)
        *DerefVal = NULL;
        
    if (DerefType != NULL)
        *DerefType = PointerValue->Typ->FromType;
        
    if (DerefOffset != NULL)
        *DerefOffset = 0;
        
    if (DerefIsLValue != NULL)
        *DerefIsLValue = TRUE;

    return PointerValue->Val->Pointer;
}

