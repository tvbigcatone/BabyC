/* sqlite3.h library for PicoC - wraps the native SQLite3 C API
 * Provides the most useful subset of the SQLite interface.
 */
#include "../../interpreter.h"
#include "sqlite3.h"
#include <stdlib.h>
#include <string.h>

/* Opaque wrappers so PicoC can treat sqlite3* and sqlite3_stmt* as pointers */
typedef struct { sqlite3 *db; } SqliteDbStruct;
typedef struct { sqlite3_stmt *stmt; } SqliteStmtStruct;

/* Definitions visible from PicoC scripts */
const char SqliteDefs[] = "\
typedef struct { void *db; } sqlite3;\n\
typedef struct { void *stmt; } sqlite3_stmt;\n\
#define SQLITE_OK           0\n\
#define SQLITE_ERROR        1\n\
#define SQLITE_INTERNAL     2\n\
#define SQLITE_PERM         3\n\
#define SQLITE_ABORT        4\n\
#define SQLITE_BUSY         5\n\
#define SQLITE_LOCKED       6\n\
#define SQLITE_NOMEM        7\n\
#define SQLITE_READONLY     8\n\
#define SQLITE_INTERRUPT    9\n\
#define SQLITE_IOERR       10\n\
#define SQLITE_CORRUPT     11\n\
#define SQLITE_NOTFOUND    12\n\
#define SQLITE_FULL        13\n\
#define SQLITE_CANTOPEN    14\n\
#define SQLITE_PROTOCOL    15\n\
#define SQLITE_EMPTY       16\n\
#define SQLITE_SCHEMA      17\n\
#define SQLITE_TOOBIG      18\n\
#define SQLITE_CONSTRAINT  19\n\
#define SQLITE_MISMATCH    20\n\
#define SQLITE_MISUSE      21\n\
#define SQLITE_NOLFS       22\n\
#define SQLITE_AUTH        23\n\
#define SQLITE_FORMAT      24\n\
#define SQLITE_RANGE       25\n\
#define SQLITE_NOTADB      26\n\
#define SQLITE_NOTICE      27\n\
#define SQLITE_WARNING     28\n\
#define SQLITE_ROW         100\n\
#define SQLITE_DONE        101\n\
#define SQLITE_INTEGER      1\n\
#define SQLITE_FLOAT        2\n\
#define SQLITE_TEXT         3\n\
#define SQLITE_BLOB         4\n\
#define SQLITE_NULL         5\n\
#define SQLITE_STATIC       0\n\
#define SQLITE_TRANSIENT   -1\n\
";

/* ---- open / close ---- */
/* Supports the real SQLite signature:
 *   int sqlite3_open(char *filename, sqlite3 **ppDb);
 * Param[0] = filename, Param[1] = pointer to sqlite3* variable (out).
 * Returns the result code (SQLITE_OK etc.).
 */
void SqliteOpen(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* Supports both forms:
     *   1) Standard:  int rc = sqlite3_open(filename, &db);
     *   2) Simplified: sqlite3 *db = sqlite3_open(filename);
     * When called with 1 argument the handle is returned as a pointer.
     * When called with 2 arguments the classic API is used (rc returned,
     * handle written through the out-parameter).
     */
    sqlite3 *db = NULL;
    const char *filename = (const char *)Param[0]->Val->Pointer;
    int rc = sqlite3_open(filename, &db);

    SqliteDbStruct *s = NULL;
    if (rc == SQLITE_OK && db != NULL) {
        s = (SqliteDbStruct *)malloc(sizeof(SqliteDbStruct));
        if (!s) {
            sqlite3_close(db);
            db = NULL;
            rc = SQLITE_NOMEM;
        } else {
            s->db = db;
        }
    } else {
        if (db) { sqlite3_close(db); db = NULL; }
        s = NULL;
    }

    if (NumArgs >= 2 && Param[1] != NULL && Param[1]->Val != NULL) {
        /* Classic form: write handle through out-parameter and return rc */
        void **out = (void **)Param[1]->Val->Pointer;
        if (out != NULL)
            *out = (void *)s;
        ReturnValue->Val->Integer = rc;
    } else {
        /* Short form (1 arg): return the handle as a pointer.
         * Even though the prototype is declared "int", PicoC allows
         * the pointer value to be assigned to a sqlite3* variable. */
        ReturnValue->Val->Pointer = (void *)s;
    }
}

void SqliteClose(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    int rc = sqlite3_close(s->db);
    free(s);
    ReturnValue->Val->Integer = rc;
}

void SqliteErrmsg(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s) {
        ReturnValue->Val->Pointer = (void *)"NULL database handle";
        return;
    }
    ReturnValue->Val->Pointer = (void *)sqlite3_errmsg(s->db);
}

void SqliteErrcode(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_errcode(s->db);
}

/* Context passed to the native SQLite callback so we can trampoline into a
 * PicoC script function.  The script function is expected to have a signature
 * compatible with the classic sqlite3_callback:
 *   int callback(void *userdata, int argc, char **argv, char **azColName);
 * (PicoC will see the two char** arguments as void* / pointers.)
 */
typedef struct {
    Picoc *pc;
    struct Value *CallbackFunc;   /* persistent Value* of the script function */
    void *UserData;               /* the 'arg' passed by the script to sqlite3_exec */
} SqliteCbCtx;

/* Native C trampoline that SQLite calls for every result row.
 * It builds the four arguments expected by a typical sqlite3_callback and
 * runs the PicoC function body.  Returns non-zero to abort the query
 * (same semantics as the real sqlite3_callback).
 */
static int SqliteNativeCallback(void *arg, int argc, char **argv, char **azColName)
{
    SqliteCbCtx *ctx = (SqliteCbCtx *)arg;
    if (ctx == NULL || ctx->CallbackFunc == NULL || ctx->pc == NULL)
        return 0;

    struct Value *FuncValue = ctx->CallbackFunc;
    if (FuncValue->Typ == NULL || FuncValue->Typ->Base != TypeFunction)
        return 0;

    struct FuncDef *fdef = &FuncValue->Val->FuncDef;
    if (fdef->Body.Pos == NULL)   /* undefined / intrinsic */
        return 0;

    /* Minimal ParseState used only for stack-frame management and error reporting */
    struct ParseState Parser;
    memset(&Parser, 0, sizeof(Parser));
    Parser.pc = ctx->pc;
    Parser.Mode = RunModeRun;

    /* Allocate everything on the heap so we do not depend on the current
     * expression-stack state (the callback is invoked from deep inside
     * the SQLite C library). */
    struct Value *RetVal = VariableAllocValueFromType(ctx->pc, &Parser, &ctx->pc->IntType, FALSE, NULL, TRUE);
    if (RetVal == NULL)
        return 0;
    RetVal->Val->Integer = 0;

    int NumParams = fdef->NumParams;
    if (NumParams > 4) NumParams = 4;   /* we only supply the classic 4 args */

    struct Value **ParamArray = NULL;
    if (NumParams > 0) {
        ParamArray = (struct Value **)HeapAllocMem(ctx->pc, sizeof(struct Value *) * NumParams);
        if (ParamArray == NULL) {
            /* leak RetVal on purpose – better than crashing */
            return 0;
        }
    }

    for (int i = 0; i < NumParams; i++) {
        struct ValueType *PType;
        if (fdef->ParamType != NULL && i < fdef->NumParams)
            PType = fdef->ParamType[i];
        else
            PType = ctx->pc->VoidPtrType;
        ParamArray[i] = VariableAllocValueFromType(ctx->pc, &Parser, PType, FALSE, NULL, TRUE);
        if (ParamArray[i] == NULL)
            continue;

        switch (i) {
            case 0: /* userdata */
                ParamArray[i]->Val->Pointer = ctx->UserData;
                break;
            case 1: /* argc */
                if (IS_NUMERIC_COERCIBLE(ParamArray[i]))
                    ParamArray[i]->Val->Integer = argc;
                break;
            case 2: /* argv */
                ParamArray[i]->Val->Pointer = (void *)argv;
                break;
            case 3: /* azColName */
                ParamArray[i]->Val->Pointer = (void *)azColName;
                break;
        }
    }

    /* Run the user function body (same pattern used by ExpressionParseFunctionCall) */
    struct ParseState FuncParser;
    ParserCopy(&FuncParser, &fdef->Body);
    FuncParser.Mode = RunModeRun;

    VariableStackFrameAdd(&Parser, "sqlite_callback", NumParams);
    if (Parser.pc->TopStackFrame != NULL) {
        Parser.pc->TopStackFrame->NumParams = NumParams;
        Parser.pc->TopStackFrame->ReturnValue = RetVal;
    }

    int OldScopeID = Parser.ScopeID;
    Parser.ScopeID = -1;
    for (int i = 0; i < NumParams; i++) {
        if (fdef->ParamName != NULL && fdef->ParamName[i] != NULL && ParamArray[i] != NULL)
            VariableDefine(ctx->pc, &Parser, fdef->ParamName[i], ParamArray[i], NULL, TRUE);
    }
    Parser.ScopeID = OldScopeID;

    if (ParseStatement(&FuncParser, TRUE) != ParseResultOk) {
        /* missing body – treat as successful no-op */
    }

    int result = 0;
    if (FuncParser.Mode == RunModeReturn ||
        (FuncParser.Mode == RunModeRun && fdef->ReturnType != &ctx->pc->VoidType)) {
        result = RetVal->Val->Integer;
    }

    VariableStackFramePop(&Parser);

    /* free the heap-allocated temporaries */
    if (ParamArray) {
        for (int i = 0; i < NumParams; i++) {
            if (ParamArray[i] != NULL)
                VariableFree(ctx->pc, ParamArray[i]);
        }
        HeapFreeMem(ctx->pc, ParamArray);
    }
    VariableFree(ctx->pc, RetVal);

    return result;   /* non-zero aborts sqlite3_exec */
}

/* ---- exec (one-shot with optional PicoC callback) ---- */
void SqliteExec(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* Real signature: int sqlite3_exec(db, sql, callback, arg, &errmsg)
     * If 'callback' is a PicoC function (passed as void*), a trampoline is
     * installed so every result row invokes the script function.
     * If errmsg out-param is provided, the error string is written there
     * (caller should free it with sqlite3_free). */
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s || s->db == NULL) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }

    char *errmsg = NULL;
    int rc;
    const char *sql = (Param[1] != NULL && Param[1]->Val != NULL)
        ? (const char *)Param[1]->Val->Pointer : NULL;
    if (sql == NULL) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }

    /* Integer 0 / NULL must NOT be treated as a function (token varargs). */
    struct Value *CbFunc = NULL;
    void *UserData = NULL;
    if (NumArgs >= 3 && Param[2] != NULL && Param[2]->Typ != NULL
        && Param[2]->Typ->Base == TypeFunction) {
        CbFunc = Param[2]->LValueFrom ? Param[2]->LValueFrom : Param[2];
    } else if (NumArgs >= 3 && Param[2] != NULL && Param[2]->Typ != NULL
               && Param[2]->Typ->Base == TypePointer
               && Param[2]->Val != NULL && Param[2]->Val->Pointer != NULL) {
        struct Value *candidate = (struct Value *)Param[2]->Val->Pointer;
        if (candidate != NULL && candidate->Typ != NULL
            && candidate->Typ->Base == TypeFunction)
            CbFunc = candidate;
    }
    if (NumArgs >= 4 && Param[3] != NULL && Param[3]->Typ != NULL
        && Param[3]->Typ->Base == TypePointer && Param[3]->Val != NULL)
        UserData = Param[3]->Val->Pointer;

    if (CbFunc != NULL) {
        SqliteCbCtx ctx;
        ctx.pc = Parser->pc;
        ctx.CallbackFunc = CbFunc;
        ctx.UserData = UserData;
        rc = sqlite3_exec(s->db, sql, SqliteNativeCallback, &ctx, &errmsg);
    } else {
        rc = sqlite3_exec(s->db, sql, NULL, NULL, &errmsg);
    }

    if (NumArgs >= 5 && Param[4] != NULL && Param[4]->Val != NULL &&
        Param[4]->Val->Pointer != NULL) {
        void **out = (void **)Param[4]->Val->Pointer;
        *out = errmsg;   /* may be NULL if no error */
    } else if (errmsg) {
        sqlite3_free(errmsg);
    }
    ReturnValue->Val->Integer = rc;
}

/* ---- prepare / step / finalize ---- */
/* Simplified: returns the statement pointer (or NULL on error).
 * Caller should check sqlite3_errcode(db) if the result is NULL.
 */
/* Real API: int sqlite3_prepare_v2(db, sql, nByte, &stmt, pzTail) */
void SqlitePrepare(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* Flexible argument handling:
     *   prepare(db, sql)
     *   prepare(db, sql, &stmt)
     *   prepare(db, sql, nByte, &stmt)
     *   prepare(db, sql, nByte, &stmt, pzTail)
     * Always returns the result code (int).
     */
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    const char *sql = (const char *)Param[1]->Val->Pointer;

    int nByte = -1;
    void **outStmt = NULL;

    if (NumArgs >= 3) {
        /* Heuristic: if the 3rd argument looks like a pointer (out-param),
         * treat it as &stmt; otherwise treat it as nByte. */
        if (Param[2]->Typ->Base == TypePointer || Param[2]->Typ->Base == TypeInt) {
            /* could be either; prefer pointer if the value looks like an address */
            if (Param[2]->Val->Pointer != NULL &&
                (Param[2]->Typ->Base == TypePointer || NumArgs == 3)) {
                outStmt = (void **)Param[2]->Val->Pointer;
            } else {
                nByte = Param[2]->Val->Integer;
            }
        }
    }
    if (NumArgs >= 4 && Param[3]->Val->Pointer != NULL) {
        outStmt = (void **)Param[3]->Val->Pointer;
    }
    /* (pzTail is ignored for simplicity) */

    sqlite3_stmt *stmt = NULL;
    int rc = sqlite3_prepare_v2(s->db, sql, nByte, &stmt, NULL);

    SqliteStmtStruct *ss = NULL;
    if (rc == SQLITE_OK && stmt != NULL) {
        ss = (SqliteStmtStruct *)malloc(sizeof(SqliteStmtStruct));
        if (!ss) {
            sqlite3_finalize(stmt);
            rc = SQLITE_NOMEM;
        } else {
            ss->stmt = stmt;
        }
    } else if (stmt) {
        sqlite3_finalize(stmt);
    }

    if (outStmt != NULL)
        *outStmt = (void *)ss;

    /* Always return the result code (int) — matches the real SQLite API:
     *   int sqlite3_prepare_v2(...);
     * The statement handle is written through the out-parameter when provided. */
    ReturnValue->Val->Integer = rc;
}

void SqliteStep(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_step(ss->stmt);
}

void SqliteFinalize(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    int rc = sqlite3_finalize(ss->stmt);
    free(ss);
    ReturnValue->Val->Integer = rc;
}

void SqliteReset(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_reset(ss->stmt);
}

void SqliteClearBindings(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_clear_bindings(ss->stmt);
}

/* ---- bind ---- */
void SqliteBindText(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* Real: int sqlite3_bind_text(stmt, idx, text, nByte, destructor)
     * Forms: (stmt,idx,text) | +nByte | +nByte,destructor
     * destructor accepted but ignored; always SQLITE_TRANSIENT (safe copy). */
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    if (NumArgs < 3) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    int idx = Param[1]->Val->Integer;
    const char *txt = (const char *)Param[2]->Val->Pointer;
    int nByte = (NumArgs >= 4) ? Param[3]->Val->Integer : -1;
    ReturnValue->Val->Integer = sqlite3_bind_text(ss->stmt, idx, txt, nByte, SQLITE_TRANSIENT);
}

void SqliteBindInt(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_bind_int(ss->stmt,
        Param[1]->Val->Integer,
        Param[2]->Val->Integer);
}

void SqliteBindDouble(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_bind_double(ss->stmt,
        Param[1]->Val->Integer,
        Param[2]->Val->FP);
}

void SqliteBindNull(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_MISUSE;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_bind_null(ss->stmt, Param[1]->Val->Integer);
}

/* ---- column access ---- */
void SqliteColumnCount(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_column_count(ss->stmt);
}

void SqliteColumnName(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    ReturnValue->Val->Pointer = (void *)sqlite3_column_name(ss->stmt, Param[1]->Val->Integer);
}

void SqliteColumnType(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = SQLITE_NULL;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_column_type(ss->stmt, Param[1]->Val->Integer);
}

void SqliteColumnText(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* Match real SQLite: SQL NULL → NULL pointer (scripts must check). */
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    ReturnValue->Val->Pointer = (void *)sqlite3_column_text(ss->stmt, Param[1]->Val->Integer);
}

void SqliteColumnInt(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_column_int(ss->stmt, Param[1]->Val->Integer);
}

void SqliteColumnDouble(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->FP = 0.0;
        return;
    }
    ReturnValue->Val->FP = sqlite3_column_double(ss->stmt, Param[1]->Val->Integer);
}

void SqliteColumnBytes(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteStmtStruct *ss = (SqliteStmtStruct *)Param[0]->Val->Pointer;
    if (!ss || !ss->stmt) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_column_bytes(ss->stmt, Param[1]->Val->Integer);
}

/* ---- helpers ---- */
void SqliteLibversion(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = (void *)sqlite3_libversion();
}

void SqliteChanges(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    ReturnValue->Val->Integer = sqlite3_changes(s->db);
}

void SqliteLastInsertRowid(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    SqliteDbStruct *s = (SqliteDbStruct *)Param[0]->Val->Pointer;
    if (!s) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    /* PicoC integer is typically 32-bit; cast for simplicity */
    ReturnValue->Val->Integer = (int)sqlite3_last_insert_rowid(s->db);
}


/* sqlite3_free - free memory returned by SQLite (e.g. errmsg from exec) */
void SqliteFree(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    if (Param[0]->Val->Pointer != NULL)
        sqlite3_free(Param[0]->Val->Pointer);
}

/* Function table registered with PicoC */
struct LibraryFunction SqliteFunctions[] =
{
    { SqliteOpen,             "int sqlite3_open(char *filename, ...);" },
    { SqliteClose,            "int sqlite3_close(sqlite3 *db);" },
    { SqliteErrmsg,           "char * sqlite3_errmsg(sqlite3 *db);" },
    { SqliteErrcode,          "int sqlite3_errcode(sqlite3 *db);" },
    { SqliteExec,             "int sqlite3_exec(sqlite3 *db, char *sql, ...);" },
    { SqlitePrepare,          "int sqlite3_prepare_v2(sqlite3 *db, char *sql, ...);" },
    { SqliteStep,             "int sqlite3_step(sqlite3_stmt *stmt);" },
    { SqliteFinalize,         "int sqlite3_finalize(sqlite3_stmt *stmt);" },
    { SqliteReset,            "int sqlite3_reset(sqlite3_stmt *stmt);" },
    { SqliteClearBindings,    "int sqlite3_clear_bindings(sqlite3_stmt *stmt);" },
    { SqliteBindText,         "int sqlite3_bind_text(sqlite3_stmt *stmt, int idx, char *val, ...);" },
    { SqliteBindInt,          "int sqlite3_bind_int(sqlite3_stmt *stmt, int idx, int val);" },
    { SqliteBindDouble,       "int sqlite3_bind_double(sqlite3_stmt *stmt, int idx, double val);" },
    { SqliteBindNull,         "int sqlite3_bind_null(sqlite3_stmt *stmt, int idx);" },
    { SqliteColumnCount,      "int sqlite3_column_count(sqlite3_stmt *stmt);" },
    { SqliteColumnName,       "char * sqlite3_column_name(sqlite3_stmt *stmt, int iCol);" },
    { SqliteColumnType,       "int sqlite3_column_type(sqlite3_stmt *stmt, int iCol);" },
    { SqliteColumnText,       "char * sqlite3_column_text(sqlite3_stmt *stmt, int iCol);" },
    { SqliteColumnInt,        "int sqlite3_column_int(sqlite3_stmt *stmt, int iCol);" },
    { SqliteColumnDouble,     "double sqlite3_column_double(sqlite3_stmt *stmt, int iCol);" },
    { SqliteColumnBytes,      "int sqlite3_column_bytes(sqlite3_stmt *stmt, int iCol);" },
    { SqliteLibversion,       "char * sqlite3_libversion(void);" },
    { SqliteChanges,          "int sqlite3_changes(sqlite3 *db);" },
    { SqliteLastInsertRowid,  "int sqlite3_last_insert_rowid(sqlite3 *db);" },
    { SqliteFree,            "void sqlite3_free(void *ptr);" },
    { NULL, NULL }
};

void SqliteSetupFunc(Picoc *pc)
{
    /* nothing special needed at the moment */
}
