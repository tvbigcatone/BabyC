/* PicoC binding for inih (Ben Hoyt)
 * #include <ini.h>
 *
 *   int  ini_parse(char *filename, void *handler, void *user);
 *        handler = script function:
 *          int handler(void *user, char *section, char *name, char *value);
 *        or NULL → values stored for ini_get*
 *
 *   int  ini_load(char *filename);           // store only (no callback)
 *   int  ini_load_string(char *str);
 *   char *ini_get(char *section, char *key);
 *   char *ini_get_string(char *section, char *key, char *def);
 *   int   ini_get_int(char *section, char *key, int def);
 *   void  ini_clear(void);
 *   int   ini_count(void);
 */
#include "../../interpreter.h"
#include "ini.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define INI_MAX_STR 256

typedef struct {
    char section[INI_MAX_STR];
    char key[INI_MAX_STR];
    char value[INI_MAX_STR];
} IniEntry;

static IniEntry *g_entries = NULL;
static int g_count = 0;
static int g_capacity = 0;

/* callback trampoline state */
typedef struct {
    Picoc *pc;
    struct Value *CallbackFunc;
    void *UserData;
} IniCbCtx;

static void ini_store_clear(void)
{
    free(g_entries);
    g_entries = NULL;
    g_count = 0;
    g_capacity = 0;
}

static int ini_store_add(const char *section, const char *name, const char *value)
{
    IniEntry *e;
    if (g_count >= g_capacity) {
        int nc = g_capacity ? g_capacity * 2 : 32;
        IniEntry *ne = (IniEntry *)realloc(g_entries, (size_t)nc * sizeof(IniEntry));
        if (!ne)
            return 0;
        g_entries = ne;
        g_capacity = nc;
    }
    e = &g_entries[g_count++];
    strncpy(e->section, section ? section : "", INI_MAX_STR - 1);
    e->section[INI_MAX_STR - 1] = '\0';
    strncpy(e->key, name ? name : "", INI_MAX_STR - 1);
    e->key[INI_MAX_STR - 1] = '\0';
    strncpy(e->value, value ? value : "", INI_MAX_STR - 1);
    e->value[INI_MAX_STR - 1] = '\0';
    return 1;
}

static int ini_store_handler(void *user, const char *section, const char *name, const char *value)
{
    (void)user;
    return ini_store_add(section, name, value);
}

static const char *ini_store_get(const char *section, const char *key)
{
    int i;
    if (!section) section = "";
    if (!key) return NULL;
    for (i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].section, section) == 0 &&
            strcmp(g_entries[i].key, key) == 0)
            return g_entries[i].value;
    }
    return NULL;
}

/* Call PicoC: int handler(void *user, char *section, char *name, char *value) */
static int IniNativeCallback(void *user, const char *section, const char *name, const char *value)
{
    IniCbCtx *ctx = (IniCbCtx *)user;
    struct Value *FuncValue;
    struct FuncDef *fdef;
    struct ParseState Parser;
    struct ParseState FuncParser;
    struct Value *RetVal;
    struct Value **ParamArray = NULL;
    int NumParams;
    int i;
    int result = 1;
    int OldScopeID;

    if (ctx == NULL || ctx->CallbackFunc == NULL || ctx->pc == NULL)
        return 1;

    FuncValue = ctx->CallbackFunc;
    if (FuncValue->Typ == NULL || FuncValue->Typ->Base != TypeFunction)
        return 1;

    fdef = &FuncValue->Val->FuncDef;
    if (fdef->Body.Pos == NULL)
        return 1;

    memset(&Parser, 0, sizeof(Parser));
    Parser.pc = ctx->pc;
    Parser.Mode = RunModeRun;

    RetVal = VariableAllocValueFromType(ctx->pc, &Parser, &ctx->pc->IntType, FALSE, NULL, TRUE);
    if (RetVal == NULL)
        return 0;
    RetVal->Val->Integer = 1;

    NumParams = fdef->NumParams;
    if (NumParams > 4)
        NumParams = 4;

    if (NumParams > 0) {
        ParamArray = (struct Value **)HeapAllocMem(ctx->pc, sizeof(struct Value *) * (size_t)NumParams);
        if (ParamArray == NULL) {
            VariableFree(ctx->pc, RetVal);
            return 0;
        }
        memset(ParamArray, 0, sizeof(struct Value *) * (size_t)NumParams);
    }

    for (i = 0; i < NumParams; i++) {
        struct ValueType *PType = (fdef->ParamType != NULL && i < fdef->NumParams)
                                  ? fdef->ParamType[i] : ctx->pc->VoidPtrType;
        ParamArray[i] = VariableAllocValueFromType(ctx->pc, &Parser, PType, FALSE, NULL, TRUE);
        if (ParamArray[i] == NULL)
            continue;
        switch (i) {
            case 0:
                ParamArray[i]->Val->Pointer = ctx->UserData;
                break;
            case 1:
                ParamArray[i]->Val->Pointer = (void *)(section ? section : "");
                break;
            case 2:
                ParamArray[i]->Val->Pointer = (void *)(name ? name : "");
                break;
            case 3:
                ParamArray[i]->Val->Pointer = (void *)(value ? value : "");
                break;
        }
    }

    ParserCopy(&FuncParser, &fdef->Body);
    FuncParser.Mode = RunModeRun;

    VariableStackFrameAdd(&Parser, "ini_handler", NumParams);
    if (Parser.pc->TopStackFrame != NULL) {
        Parser.pc->TopStackFrame->NumParams = NumParams;
        Parser.pc->TopStackFrame->ReturnValue = RetVal;
    }

    OldScopeID = Parser.ScopeID;
    Parser.ScopeID = -1;
    for (i = 0; i < NumParams; i++) {
        if (fdef->ParamName != NULL && fdef->ParamName[i] != NULL && ParamArray[i] != NULL)
            VariableDefine(ctx->pc, &Parser, fdef->ParamName[i], ParamArray[i], NULL, TRUE);
    }
    Parser.ScopeID = OldScopeID;

    if (ParseStatement(&FuncParser, TRUE) != ParseResultOk) {
        /* treat as success */
    }

    if (FuncParser.Mode == RunModeReturn ||
        (FuncParser.Mode == RunModeRun && fdef->ReturnType != &ctx->pc->VoidType)) {
        result = RetVal->Val->Integer;
    }

    VariableStackFramePop(&Parser);

    if (ParamArray) {
        for (i = 0; i < NumParams; i++) {
            if (ParamArray[i] != NULL)
                VariableFree(ctx->pc, ParamArray[i]);
        }
        HeapFreeMem(ctx->pc, ParamArray);
    }
    VariableFree(ctx->pc, RetVal);

    /* also keep a copy in the store so ini_get works after parse */
    ini_store_add(section, name, value);

    return result; /* 0 = abort parse (inih convention) */
}

/* ---- PicoC wrappers ---- */

void Ini_parse(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *filename = (const char *)Param[0]->Val->Pointer;
    struct Value *HandlerArg = (NumArgs >= 2) ? Param[1] : NULL;
    void *user = (NumArgs >= 3) ? Param[2]->Val->Pointer : NULL;
    int rc;
    IniCbCtx ctx;

    ini_store_clear();

    if (!filename) {
        ReturnValue->Val->Integer = -1;
        return;
    }

    /* Is the 2nd arg a PicoC function?
     * Direct TypeFunction, or void* holding a coerced function Value*
     * (see ExpressionAssign function→pointer). */
    {
        struct Value *HandlerVal = NULL;
        if (HandlerArg != NULL && HandlerArg->Typ != NULL) {
            if (HandlerArg->Typ->Base == TypeFunction && HandlerArg->Val != NULL)
                HandlerVal = HandlerArg->LValueFrom ? HandlerArg->LValueFrom : HandlerArg;
            else if (HandlerArg->Val != NULL && HandlerArg->Val->Pointer != NULL) {
                struct Value *maybe = (struct Value *)HandlerArg->Val->Pointer;
                if (maybe->Typ != NULL && maybe->Typ->Base == TypeFunction)
                    HandlerVal = maybe;
            }
        }
        if (HandlerVal != NULL && HandlerVal->Val != NULL &&
            HandlerVal->Val->FuncDef.Body.Pos != NULL) {
            ctx.pc = Parser->pc;
            ctx.CallbackFunc = HandlerVal;
            ctx.UserData = user;
            rc = ini_parse(filename, IniNativeCallback, &ctx);
        } else {
            rc = ini_parse(filename, ini_store_handler, NULL);
        }
    }

    ReturnValue->Val->Integer = rc;
}

void Ini_load(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *filename = (const char *)Param[0]->Val->Pointer;
    int rc;
    ini_store_clear();
    if (!filename) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    rc = ini_parse(filename, ini_store_handler, NULL);
    ReturnValue->Val->Integer = rc;
}

void Ini_load_string(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *str = (const char *)Param[0]->Val->Pointer;
    int rc;
    ini_store_clear();
    if (!str) {
        ReturnValue->Val->Integer = -1;
        return;
    }
    rc = ini_parse_string(str, ini_store_handler, NULL);
    ReturnValue->Val->Integer = rc;
}

void Ini_get(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *section = (const char *)Param[0]->Val->Pointer;
    const char *key = (const char *)Param[1]->Val->Pointer;
    const char *v = ini_store_get(section, key);
    ReturnValue->Val->Pointer = (void *)v;
}

void Ini_get_string(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *section = (const char *)Param[0]->Val->Pointer;
    const char *key = (const char *)Param[1]->Val->Pointer;
    const char *def = (const char *)Param[2]->Val->Pointer;
    const char *v = ini_store_get(section, key);
    ReturnValue->Val->Pointer = (void *)(v ? v : def);
}

void Ini_get_int(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    const char *section = (const char *)Param[0]->Val->Pointer;
    const char *key = (const char *)Param[1]->Val->Pointer;
    int def = Param[2]->Val->Integer;
    const char *v = ini_store_get(section, key);
    if (!v) {
        ReturnValue->Val->Integer = def;
        return;
    }
    ReturnValue->Val->Integer = atoi(v);
}

void Ini_clear(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    (void)Parser; (void)ReturnValue; (void)Param; (void)NumArgs;
    ini_store_clear();
}

void Ini_count(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    (void)Parser; (void)Param; (void)NumArgs;
    ReturnValue->Val->Integer = g_count;
}

const char IniDefs[] = "\
";

struct LibraryFunction IniFunctions[] =
{
    { Ini_parse,       "int ini_parse(char *, void *, void *);" },
    { Ini_load,        "int ini_load(char *);" },
    { Ini_load_string, "int ini_load_string(char *);" },
    { Ini_get,         "char *ini_get(char *, char *);" },
    { Ini_get_string,  "char *ini_get_string(char *, char *, char *);" },
    { Ini_get_int,     "int ini_get_int(char *, char *, int);" },
    { Ini_clear,       "void ini_clear();" },
    { Ini_count,       "int ini_count();" },
    { NULL, NULL }
};

void IniSetupFunc(Picoc *pc)
{
    (void)pc;
}
