/* ir_shadow.c — Variable table + dual-write shadow
 * Not on ir_fast dispatch chain; only eval/stmt/token interop edges.
 */
#include "picoc.h"
#include <string.h>
#include <stdlib.h>
#include "ir_runtime.h"
#include "ir_bridge.h"

void ExpressionInitMarkAddr(struct Value *Root, void *Addr, int Size);
void ExpressionMarkInitialized(struct Value *Dest);

extern long IrGlobalBindHits;
extern long IrGlobalBindMiss;

#define IR_VAR_CACHE_SLOTS 16
static const char *IrVarCacheIdent[IR_VAR_CACHE_SLOTS];
static struct Value *IrVarCacheVal[IR_VAR_CACHE_SLOTS];
static struct StackFrame *IrVarCacheFrame[IR_VAR_CACHE_SLOTS];
static unsigned char IrVarCacheNext;

int IrVarTableLookup(Picoc *pc, const char *Ident, struct Value **Out)
{
    struct Value *V = NULL;
    struct StackFrame *Frame;
    int i;
    if (Ident == NULL || Out == NULL)
        return 0;
    *Out = NULL;
    Frame = pc->TopStackFrame;
    for (i = 0; i < IR_VAR_CACHE_SLOTS; i++)
    {
        if (IrVarCacheIdent[i] == Ident
            && IrVarCacheFrame[i] == Frame
            && IrVarCacheVal[i] != NULL
            && !IrVarCacheVal[i]->OutOfScope)
        {
            *Out = IrVarCacheVal[i];
            if (i != (IrVarCacheNext + IR_VAR_CACHE_SLOTS - 1) % IR_VAR_CACHE_SLOTS)
            {
                int j = (IrVarCacheNext + IR_VAR_CACHE_SLOTS - 1) % IR_VAR_CACHE_SLOTS;
                const char *ti = IrVarCacheIdent[j];
                struct Value *tv = IrVarCacheVal[j];
                struct StackFrame *tf = IrVarCacheFrame[j];
                IrVarCacheIdent[j] = IrVarCacheIdent[i];
                IrVarCacheVal[j] = IrVarCacheVal[i];
                IrVarCacheFrame[j] = IrVarCacheFrame[i];
                IrVarCacheIdent[i] = ti;
                IrVarCacheVal[i] = tv;
                IrVarCacheFrame[i] = tf;
            }
            return 1;
        }
    }
    if (Frame != NULL
        && TableGet(&Frame->LocalTable, Ident, &V, NULL, NULL, NULL)
        && V != NULL && !V->OutOfScope)
    {
        *Out = V;
    }
    else if (TableGet(&pc->GlobalTable, Ident, &V, NULL, NULL, NULL) && V != NULL)
    {
        *Out = V;
    }
    else
        return 0;
    i = IrVarCacheNext % IR_VAR_CACHE_SLOTS;
    IrVarCacheIdent[i] = Ident;
    IrVarCacheVal[i] = V;
    IrVarCacheFrame[i] = Frame;
    IrVarCacheNext = (unsigned char)((i + 1) % IR_VAR_CACHE_SLOTS);
    return 1;
}


int IrVarTableOutOfScope(Picoc *pc, const char *Ident)
{
    struct Table *HashTable;
    struct TableEntry *Entry;
    int Count;
    if (Ident == NULL || pc == NULL)
        return 0;
    HashTable = (pc->TopStackFrame == NULL)
        ? &pc->GlobalTable : &pc->TopStackFrame->LocalTable;
    for (Count = 0; Count < HashTable->Size; Count++)
    {
        for (Entry = HashTable->HashTable[Count]; Entry != NULL; Entry = Entry->Next)
        {
            if (Entry->p.v.Val != NULL && Entry->p.v.Val->OutOfScope
                && (char *)((intptr_t)Entry->p.v.Key & ~(intptr_t)1) == Ident)
                return 1;
        }
    }
    return 0;
}


int IrVarTableDefined(Picoc *pc, const char *Ident)
{
    struct Value *V = NULL;
    return IrVarTableLookup(pc, Ident, &V);
}


void IrVarTableGet(struct ParseState *Parser, const char *Ident,
                          struct Value **Out)
{
    if (!IrVarTableLookup(Parser->pc, Ident, Out))
    {
        if (IrVarTableOutOfScope(Parser->pc, Ident))
            ProgramFail(Parser, "'%s' is out of scope", Ident);
        else
            ProgramFail(Parser, "'%s' is undefined", Ident);
    }
}


struct Value *IrGlobalVal(struct ParseState *Parser, IrExpr *E)
{
    struct Value *V;
    if (E == NULL || E->Ident == NULL)
        return NULL;
    V = E->CachedVal;
    if (V != NULL && !V->OutOfScope)
    {
        IrGlobalBindHits++;
        return V;
    }
    IrVarTableGet(Parser, E->Ident, &V);
    E->CachedVal = V;
    IrGlobalBindMiss++;
    return V;
}

/* Direct callee by name, shared by tree (IrExCall) and linear (IrLinCall
 * lowered with an unbound name). Resolved names cost one soft lookup; an
 * unresolved one fails with the tree's "'name' is undefined" (or "out of
 * scope") with the caret on the end of the calling statement, independent
 * of whatever position the previous statement left in the parser. */
struct Value *IrCalleeLookupOrFail(struct ParseState *Parser, const char *Name,
                                   int Line, int CharPos)
{
    struct Value *V = IrGlobalIdentSoft(Parser, Name);
    if (V != NULL)
        return V;
    if (Line > 0)
        Parser->Line = Line;
    if (CharPos > 0)
        Parser->CharacterPos = CharPos;
    ParserSetCaretOnStmtEnd(Parser);
    IrVarTableGet(Parser, Name, &V);
    return V;
}

/* Soft: CachedVal-first, no ProgramFail on miss. Use for optional global
 * resolution on hot paths instead of IrVarTableDefined + IrGlobalVal. */
struct Value *IrGlobalValSoft(struct ParseState *Parser, IrExpr *E)
{
    struct Value *V = NULL;
    if (E == NULL || E->Ident == NULL)
        return NULL;
    V = E->CachedVal;
    if (V != NULL && !V->OutOfScope)
    {
        IrGlobalBindHits++;
        return V;
    }
    if (!IrVarTableLookup(Parser->pc, E->Ident, &V) || V == NULL || V->OutOfScope)
        return NULL;
    E->CachedVal = V;
    IrGlobalBindMiss++;
    return V;
}

/* Name-only soft lookup (no ProgramFail). Prefer over IrVarTableDefined on hot paths. */
struct Value *IrGlobalIdentSoft(struct ParseState *Parser, const char *Ident)
{
    struct Value *V = NULL;
    if (Ident == NULL || Parser == NULL)
        return NULL;
    if (!IrVarTableLookup(Parser->pc, Ident, &V) || V == NULL || V->OutOfScope)
        return NULL;
    return V;
}

/* F3/F4: single cold-fill for name caches (FastCond/FastInc/bound).
 * Hit returns the cached Value*; miss is the only IrVarTableGet. */
struct Value *IrGlobalCacheFill(struct ParseState *Parser, const char *Ident,
                                struct Value **Cache)
{
    struct Value *V;
    if (Cache != NULL && *Cache != NULL && !(*Cache)->OutOfScope)
    {
        IrGlobalBindHits++;
        return *Cache;
    }
    if (Ident == NULL)
        return NULL;
    IrVarTableGet(Parser, Ident, &V);
    if (Cache != NULL)
        *Cache = V;
    IrGlobalBindMiss++;
    return V;
}


static int IrShadowEnabled(void)
{
    static int Cached = -1;
    const char *e;
    if (Cached >= 0)
        return Cached;
    e = getenv("BABYC_IR_SHADOW");
    /* unset or empty or non-"0" => enabled; only explicit "0" disables */
    if (e != NULL && e[0] == '0' && e[1] == '\0')
        Cached = 0;
    else
        Cached = 1;
    return Cached;
}


struct Value *IrShadowGet(struct ParseState *Parser, const char *Ident)
{
    struct Value *V = NULL;
    if (!IrShadowEnabled() || Ident == NULL)
        return NULL;
    if (!IrVarTableDefined(Parser->pc, Ident))
        return NULL;
    IrVarTableGet(Parser, Ident, &V); /* shadow bridge */
    return V;
}


void IrShadowMarkAddrEx(struct ParseState *Parser, const char *Ident,
                               void *Addr, int Size, int WholeIfNoMap)
{
    struct Value *SV;
    if (Ident == NULL || Addr == NULL || Size <= 0)
        return;
    SV = IrShadowGet(Parser, Ident); /* shadow bridge */
    if (SV == NULL)
        return;
    if (SV->ElemInit != NULL)
        ExpressionInitMarkAddr(SV, Addr, Size);
    else if (WholeIfNoMap)
        ExpressionMarkInitialized(SV);
}


void IrShadowMarkAddr(struct ParseState *Parser, const char *Ident,
                             void *Addr, int Size)
{
    IrShadowMarkAddrEx(Parser, Ident, Addr, Size, 0);
}


void IrValueMarkAddr(struct Value *SV, void *Addr, int Size)
{
    if (!IrShadowEnabled())
        return;
    if (SV == NULL || Addr == NULL || Size <= 0)
        return;
    if (SV->ElemInit != NULL)
        ExpressionInitMarkAddr(SV, Addr, Size);
    else
        ExpressionMarkInitialized(SV);
}


void IrValueMarkAll(struct Value *SV)
{
    if (!IrShadowEnabled())
        return;
    if (SV != NULL)
        ExpressionMarkInitialized(SV);
}


