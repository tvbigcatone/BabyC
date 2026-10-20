/* picoc's interface to the underlying platform. most platform-specific code
 * is in platform/platform_XX.c and platform/library_XX.c */
 
#include "picoc.h"
#include "interpreter.h"
#ifndef BABYC_MICRO
# include "platform/guest_thread.h"
#endif

#ifdef WITH_SQLITE
# if defined(__has_include)
#  if __has_include("third_party/sqlite/sqlite3.h")
#   include "third_party/sqlite/sqlite3.h"
#  elif __has_include("sqlite3.h")
#   include "sqlite3.h"
#  endif
# else
#  include "third_party/sqlite/sqlite3.h"
# endif
#endif
/* BUILD_TIME may be provided via -DBUILD_TIME=... or cstdlib_ext/build.h in main */


/* initialise everything */
void PicocInitialise(Picoc *pc, int StackSize)
{
    memset(pc, '\0', sizeof(*pc));
    pc->NextScopeID = 1;
    PlatformInit(pc);
    BasicIOInit(pc);
    HeapInit(pc, StackSize);
    TableInit(pc);
    VariableInit(pc);
    LexInit(pc);
    TypeInit(pc);
#ifndef NO_HASH_INCLUDE
    IncludeInit(pc);
#endif
    LibraryInit(pc);
#ifdef BUILTIN_MINI_STDLIB
    LibraryAdd(pc, &GlobalTable, "c library", &CLibrary[0]);
    CLibraryInit(pc);
#endif
    PlatformLibraryInit(pc);
    DebugInit(pc);
}

/* free memory */
void PicocCleanup(Picoc *pc)
{
    DebugCleanup(pc);
#ifndef NO_HASH_INCLUDE
    IncludeCleanup(pc);
#endif
    ParseCleanup(pc);
    LexCleanup(pc);
    VariableCleanup(pc);
    TypeCleanup(pc);
    TableStrFree(pc);
    HeapCleanup(pc);
    PlatformCleanup(pc);
}

/* platform-dependent code for running programs */
#if defined(UNIX_HOST) || defined(WIN32)

#define CALL_MAIN_NO_ARGS_RETURN_VOID "main();"
#define CALL_MAIN_WITH_ARGS_RETURN_VOID "main(__argc,__argv);"
#define CALL_MAIN_NO_ARGS_RETURN_INT "__exit_value = main();"
#define CALL_MAIN_WITH_ARGS_RETURN_INT "__exit_value = main(__argc,__argv);"

void PicocCallMain(Picoc *pc, int argc, char **argv)
{
    struct Value* FuncValue = NULL;
    /* When the host passes no args, still provide argv[0] like a real C runtime */
    static char *DummyArgv[1] = { "picoc" };
    int EffectiveArgc = argc;
    char **EffectiveArgv = argv;

    if (!VariableDefined(pc, TableStrRegister(pc, "main")))
        ProgramFailNoParser(pc, "main() is not defined");

    VariableGet(pc, NULL, TableStrRegister(pc, "main"), &FuncValue);
    if (FuncValue == NULL || FuncValue->Typ == NULL || FuncValue->Typ->Base != TypeFunction)
        ProgramFailNoParser(pc, "main is not a function - can't call it");

    if (EffectiveArgc <= 0 || EffectiveArgv == NULL)
    {
        EffectiveArgc = 1;
        EffectiveArgv = DummyArgv;
    }

    if (FuncValue->Val->FuncDef.NumParams != 0)
    {
        /* define the arguments */
        VariableDefinePlatformVar(pc, NULL, "__argc", &pc->IntType, (union AnyValue *)&EffectiveArgc, FALSE);
        VariableDefinePlatformVar(pc, NULL, "__argv", pc->CharPtrPtrType, (union AnyValue *)&EffectiveArgv, FALSE);
    }

#ifndef BABYC_MICRO
    BabycGuestSetPC(pc);
    BabycGuestGIL_Lock();
#endif
    if (FuncValue->Val->FuncDef.ReturnType == &pc->VoidType)
    {
        if (FuncValue->Val->FuncDef.NumParams == 0)
            PicocParse(pc, "startup", CALL_MAIN_NO_ARGS_RETURN_VOID, strlen(CALL_MAIN_NO_ARGS_RETURN_VOID), TRUE, TRUE, FALSE, TRUE);
        else
            PicocParse(pc, "startup", CALL_MAIN_WITH_ARGS_RETURN_VOID, strlen(CALL_MAIN_WITH_ARGS_RETURN_VOID), TRUE, TRUE, FALSE, TRUE);
    }
    else
    {
        VariableDefinePlatformVar(pc, NULL, "__exit_value", &pc->IntType, (union AnyValue *)&pc->PicocExitValue, TRUE);
    
        if (FuncValue->Val->FuncDef.NumParams == 0)
            PicocParse(pc, "startup", CALL_MAIN_NO_ARGS_RETURN_INT, strlen(CALL_MAIN_NO_ARGS_RETURN_INT), TRUE, TRUE, FALSE, TRUE);
        else
            PicocParse(pc, "startup", CALL_MAIN_WITH_ARGS_RETURN_INT, strlen(CALL_MAIN_WITH_ARGS_RETURN_INT), TRUE, TRUE, FALSE, TRUE);
    }
#ifndef BABYC_MICRO
    BabycGuestGIL_Unlock();
#endif
}
#endif

void PrintSourceTextErrorLine(IOFILE *Stream, const char *FileName, const char *SourceText, int Line, int CharacterPos)
{
    int LineCount;
    const char *LinePos;
    const char *CPos;
    int CCount;
    
    if (SourceText != NULL)
    {
        /* find the source line (CRLF-safe: only count \n) */
        for (LinePos = SourceText, LineCount = 1; *LinePos != '\0' && LineCount < Line; LinePos++)
        {
            if (*LinePos == '\n')
                LineCount++;
        }
        /* skip leading \r if LinePos landed on CR of a CRLF pair (shouldn't) */
        if (*LinePos == '\r')
            LinePos++;

        /* If the reported line is empty/whitespace-only (skewed Line from a
         * past lexer bug, or error landed between statements), walk back to
         * the nearest non-empty line so the caret is not orphaned. */
        {
            const char *Try = LinePos;
            int back = 0;
            while (Try > SourceText && back < 8)
            {
                const char *S;
                int empty = 1;
                for (S = Try; *S != '\n' && *S != '\r' && *S != '\0'; S++)
                {
                    if (*S != ' ' && *S != '\t')
                    { empty = 0; break; }
                }
                if (!empty)
                    break;
                /* step back over previous line ending */
                if (Try > SourceText && *(Try-1) == '\n')
                    Try--;
                if (Try > SourceText && *(Try-1) == '\r')
                    Try--;
                while (Try > SourceText && *(Try-1) != '\n')
                    Try--;
                back++;
                LinePos = Try;
            }
        }
        
        /* display the line — stop at CR or LF so Windows CRLF does not emit bare CR */
        for (CPos = LinePos; *CPos != '\n' && *CPos != '\r' && *CPos != '\0'; CPos++)
            PrintCh(*CPos, Stream);
        PrintCh('\n', Stream);
        
        /* display the error position (clamp to line length) */
        {
            int LineLen = 0;
            for (CPos = LinePos; *CPos != '\n' && *CPos != '\r' && *CPos != '\0'; CPos++)
                LineLen++;
            if (CharacterPos > LineLen && LineLen > 0)
                CharacterPos = LineLen;
        }
        for (CPos = LinePos, CCount = 0;
             *CPos != '\n' && *CPos != '\r' && *CPos != '\0'
             && (CCount < CharacterPos || *CPos == ' ');
             CPos++, CCount++)
        {
            if (*CPos == '\t')
                PrintCh('\t', Stream);
            else
                PrintCh(' ', Stream);
        }
    }
    else
    {
        /* assume we're in interactive mode - try to make the arrow match up with the input text */
        for (CCount = 0; CCount < CharacterPos + (int)strlen(INTERACTIVE_PROMPT_STATEMENT); CCount++)
            PrintCh(' ', Stream);
    }
    /* Print basename only so expect files are portable across absolute vs relative paths */
    {
        const char *Base = FileName;
        const char *S;
        if (Base != NULL)
        {
            for (S = Base; *S != '\0'; S++)
            {
                if (*S == '/' || *S == '\\')
                    Base = S + 1;
            }
        }
        else
            Base = "input";
        PlatformPrintf(Stream, "^\n%s:%d:%d ", Base, Line, CharacterPos);
    }
}


/* Point caret at the LHS of an assignment on the current line (for write traps
 * on string literals, const, etc.). Prefer first '[' before '=', else first
 * non-space token; never leave CharacterPos past '//' comments. */
void ParserSetCaretOnAssignTarget(struct ParseState *Parser)
{
    const char *Src, *LinePos, *CPos, *Eq, *Stop, *Best;
    int LineCount, Col;

    if (Parser == NULL || Parser->SourceText == NULL)
        return;
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1; *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n')
            LineCount++;
    if (*LinePos == '\r')
        LinePos++;

    /* line ends at // comment, ;, or newline */
    Stop = LinePos;
    for (CPos = LinePos; *CPos != '\0' && *CPos != '\n' && *CPos != '\r'; CPos++)
    {
        if (CPos[0] == '/' && CPos[1] == '/')
            break;
        if (*CPos == ';')
            break;
        Stop = CPos + 1;
    }

    Eq = NULL;
    for (CPos = LinePos; CPos < Stop; CPos++)
        if (*CPos == '=')
            Eq = CPos;
    if (Eq == NULL)
        Eq = Stop;

    /* Prefer '[' on the LHS (p[i] = ...): last '[' before '=' */
    Best = NULL;
    for (CPos = LinePos; CPos < Eq; CPos++)
        if (*CPos == '[')
            Best = CPos;
    if (Best == NULL)
    {
        /* else first non-space on the line (start of LHS) */
        for (CPos = LinePos; CPos < Eq && (*CPos == ' ' || *CPos == '\t'); CPos++)
            ;
        Best = CPos;
    }
    if (Best == NULL || Best >= Stop)
        return;
    Col = (int)(Best - LinePos) + 1;
    if (Col < 1)
        Col = 1;
    Parser->CharacterPos = Col;
}

/* Set Parser caret to a meaningful column on the current source line.
 * Prefer pointing at the last occurrence of PreferCh (e.g. '%' or '/') before
 * ';' or end of line; if not found, leave CharacterPos unchanged. */
void ParserSetCaretOnIdent(struct ParseState *Parser, const char *Ident)
{
    const char *Src, *LinePos, *CPos, *Best;
    int LineCount, Col, N;
    if (Parser == NULL || Parser->SourceText == NULL || Ident == NULL || Ident[0] == '\0')
        return;
    N = (int)strlen(Ident);
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1; *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n') LineCount++;
    Best = NULL;
    for (CPos = LinePos; *CPos != '\0' && *CPos != '\n'; CPos++)
    {
        if (strncmp(CPos, Ident, (size_t)N) == 0)
        {
            char before = (CPos > LinePos) ? CPos[-1] : ' ';
            char after = CPos[N];
            int okb = !((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z') || before == '_' || (before >= '0' && before <= '9'));
            int oka = !((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z') || after == '_' || (after >= '0' && after <= '9'));
            if (okb && oka)
                Best = CPos;
        }
    }
    if (Best == NULL) return;
    Col = (int)(Best - LinePos) + 1;
    if (Col < 1) Col = 1;
    Parser->CharacterPos = Col;
}

/* Single source-location resolver for runtime diagnostics emitted by any
 * execution engine.  IR/VM can identify the root object more reliably than a
 * token column; resolve the leaf member from the original source line here,
 * rather than letting each engine guess a column independently. */
/* Canonical setter for source locations already captured by the parser/IR.
 * Runtime engines should pass coordinates from source metadata, never infer
 * columns from variable names or rendered text. This helper has no hot-path
 * cost unless a diagnostic is actually emitted. */
void ParserSetDiagnosticLocation(struct ParseState *Parser, int Line, int Column)
{
    if (Parser == NULL) return;
    if (Line > 0) Parser->Line = Line;
    if (Column > 0) Parser->CharacterPos = Column;
}


/* Caret on last ']' of an index access after BaseIdent on the current line
 * (matches tree multi-dim uninit: m[i][j] → closing bracket, 250/252). */
void ParserSetCaretOnIndexAccess(struct ParseState *Parser, const char *BaseIdent)
{
    const char *Src, *LinePos, *CPos, *Best = NULL;
    int LineCount, start;
    if (Parser == NULL || Parser->SourceText == NULL)
        return;
    if (BaseIdent != NULL && BaseIdent[0] != '\0')
        ParserSetCaretOnIdent(Parser, BaseIdent);
    start = Parser->CharacterPos > 0 ? Parser->CharacterPos - 1 : 0;
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1; *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n') LineCount++;
    for (CPos = LinePos + start; *CPos && *CPos != '\n'; CPos++)
    {
        if (*CPos == ']') Best = CPos;
        if (*CPos == ',' || *CPos == ';' || *CPos == ')') break;
    }
    if (Best != NULL)
        Parser->CharacterPos = (int)(Best - LinePos) + 1;
}

/* Caret on the ';' that ends the statement starting at or before
 * Parser->CharacterPos on Parser->Line (column of the token parser at the end
 * of the statement, as the tree reports name-lookup failures). No-op when the
 * line has no ';' after that column. */
void ParserSetCaretOnStmtEnd(struct ParseState *Parser)
{
    const char *LinePos, *CPos;
    int LineCount, start;
    if (Parser == NULL || Parser->SourceText == NULL || Parser->Line <= 0)
        return;
    for (LinePos = Parser->SourceText, LineCount = 1;
         *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n')
            LineCount++;
    start = Parser->CharacterPos > 0 ? Parser->CharacterPos - 1 : 0;
    for (CPos = LinePos; *CPos && *CPos != '\n' && CPos - LinePos < start; CPos++)
        ;
    for (; *CPos && *CPos != '\n'; CPos++)
        if (*CPos == ';')
        {
            Parser->CharacterPos = (int)(CPos - LinePos) + 1;
            return;
        }
}

void ParserSetCaretOnLine(struct ParseState *Parser, int PreferCh)
{
    const char *Src;
    const char *LinePos;
    const char *CPos;
    const char *Best;
    int LineCount;
    int Col;

    if (Parser == NULL || Parser->SourceText == NULL)
        return;
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1; *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
    {
        if (*LinePos == '\n')
            LineCount++;
    }
    Best = NULL;
    for (CPos = LinePos; *CPos != '\0' && *CPos != '\n'; CPos++)
    {
        if ((unsigned char)*CPos == (unsigned char)PreferCh)
            Best = CPos;
        if (*CPos == ';')
            break;
    }
    if (Best == NULL)
        return;
    /* point at operand after operator if present (space + ident) */
    CPos = Best + 1;
    while (*CPos == ' ' || *CPos == '\t')
        CPos++;
    if ((*CPos >= 'a' && *CPos <= 'z') || (*CPos >= 'A' && *CPos <= 'Z')
        || *CPos == '_' || (*CPos >= '0' && *CPos <= '9'))
        Best = CPos;
    Col = (int)(Best - LinePos) + 1; /* 1-based column like Lex CharacterPos */
    if (Col < 1)
        Col = 1;
    Parser->CharacterPos = Col;
}

/* Engine-neutral fatal-error endpoint. Runtime engines with source metadata
 * should call ProgramFailAt directly; ProgramFail is the legacy parser adapter. */
static void ProgramFailAtV(Picoc *pc, const DiagnosticLocation *Location,
                           const char *Message, va_list Args)
{
    DiagnosticReportV(pc, Location, Message, Args);
    if (pc == NULL) exit(1);
    PlatformExit(pc, 1);
}

void ProgramFailAt(Picoc *pc, const DiagnosticLocation *Location,
                   const char *Message, ...)
{
    va_list Args;
    va_start(Args, Message);
    ProgramFailAtV(pc, Location, Message, Args);
    va_end(Args); /* unreachable for fatal errors; kept for analyzers */
    exit(1);
}

void ProgramFail(struct ParseState *Parser, const char *Message, ...)
{
    va_list Args;
    DiagnosticLocation Location;
    DiagnosticLocation *LocationPtr = NULL;
    Picoc *pc = Parser != NULL ? Parser->pc : NULL;

    if (Parser != NULL)
    {
        Location.FileName = Parser->FileName;
        Location.SourceText = Parser->SourceText;
        Location.Line = Parser->Line;
        Location.Column = Parser->CharacterPos;
        LocationPtr = &Location;
    }
    va_start(Args, Message);
    ProgramFailAtV(pc, LocationPtr, Message, Args);
    va_end(Args); /* unreachable for fatal errors; kept for analyzers */
    exit(1);
}

/* Fatal errors without a parser use the same diagnostic ingress, but have no
 * source span to render. */
void ProgramFailNoParser(Picoc *pc, const char *Message, ...)
{
    va_list Args;
    va_start(Args, Message);
    DiagnosticReportV(pc, NULL, Message, Args);
    va_end(Args);
    if (pc == NULL) exit(1);
    PlatformExit(pc, 1);
}

/* like ProgramFail() but gives descriptive error messages for assignment */
void AssignFail(struct ParseState *Parser, const char *Format, struct ValueType *Type1, struct ValueType *Type2, int Num1, int Num2, const char *FuncName, int ParamNo)
{
    DiagnosticLocation Location;
    DiagnosticLocation *LocationPtr = NULL;
    Picoc *pc = Parser != NULL ? Parser->pc : NULL;

    if (Parser != NULL)
    {
        Location.FileName = Parser->FileName;
        Location.SourceText = Parser->SourceText;
        Location.Line = Parser->Line;
        Location.Column = Parser->CharacterPos;
        LocationPtr = &Location;
    }

    /* Assignment failures share the diagnostic renderer and source-location
     * policy with ProgramFail(), including custom %t type formatting. */
    DiagnosticReportAssign(pc, LocationPtr, Format, Type1, Type2,
                           Num1, Num2, FuncName, ParamNo);
    if (pc != NULL)
        PlatformExit(pc, 1);
}

/* exit lexing with a message */
void LexFail(Picoc *pc, struct LexState *Lexer, const char *Message, ...)
{
    va_list Args;
    DiagnosticLocation Location;
    LexCleanupOnFailure(pc);

    Location.FileName = Lexer->FileName;
    Location.SourceText = Lexer->SourceText;
    Location.Line = Lexer->Line;
    Location.Column = Lexer->CharacterPos;
    va_start(Args, Message);
    DiagnosticReportV(pc, &Location, Message, Args);
    va_end(Args);
    PlatformExit(pc, 1);
}

/* printf for compiler error reporting */
void PlatformPrintf(IOFILE *Stream, const char *Format, ...)
{
    va_list Args;
    
    va_start(Args, Format);
    PlatformVPrintf(Stream, Format, Args);
    va_end(Args);
}

void PlatformVPrintf(IOFILE *Stream, const char *Format, va_list Args)
{
    const char *FPos;
    
    for (FPos = Format; *FPos != '\0'; FPos++)
    {
        if (*FPos == '%')
        {
            FPos++;
            switch (*FPos)
            {
            case 's': PrintStr(va_arg(Args, char *), Stream); break;
            case 'd': PrintSimpleInt(va_arg(Args, int), Stream); break;
            case 'c': PrintCh(va_arg(Args, int), Stream); break;
            case 't': PrintType(va_arg(Args, struct ValueType *), Stream); break;
#ifndef NO_FP
            case 'f': PrintFP(va_arg(Args, double), Stream); break;
#endif
            case '%': PrintCh('%', Stream); break;
            case '\0': FPos--; break;
            }
        }
        else
            PrintCh(*FPos, Stream);
    }
}

/* make a new temporary name. takes a static buffer of char [7] as a parameter. should be initialised to "XX0000"
 * where XX can be any characters */
char *PlatformMakeTempName(Picoc *pc, char *TempNameBuffer)
{
    int CPos = 5;
    
    while (CPos > 1)
    {
        if (TempNameBuffer[CPos] < '9')
        {
            TempNameBuffer[CPos]++;
            return TableStrRegister(pc, TempNameBuffer);
        }
        else
        {
            TempNameBuffer[CPos] = '0';
            CPos--;
        }
    }

    return TableStrRegister(pc, TempNameBuffer);
}

#ifdef PERSOPORT
char INTERACTIVE_PROMPT_STATEMENT[32] = "pico-c> " ;
char INTERACTIVE_PROMPT_LINE[32] = "      > " ;
void PrintHelp( char * filename, struct LibraryFunction DefFunctions[], const char * Defs ) {
	int i=0 ;
	if( filename != NULL ) { printf( "/* %s prototypes */\n", filename ) ; }
	if( Defs != NULL ) { printf( "%s\n", Defs ) ; }
	if( DefFunctions != NULL )
		while( DefFunctions[i].Prototype != NULL ) {
			printf( "%s\n", DefFunctions[i].Prototype ) ;
			i++;
		}
	printf( "\n" ) ;
}
#endif


/* Print startup banner with PicoC and optional third-party versions.
 * Only libraries compiled in (WITH_*) are listed. */
void PicocPrintBanner(void)
{
    printf("========================================\n");
    printf("  pico-c %s\n", PICOC_VERSION);
#ifdef BUILD_TIME
    printf("  build:  %s\n", BUILD_TIME);
#endif
    printf("----------------------------------------\n");
    printf("  third-party libraries:\n");

#ifdef WITH_SQLITE
# ifndef SQLITE_VERSION
    /* may already be included via sqlite3.h elsewhere; define fallback */
#  define SQLITE_VERSION "(linked)"
# endif
    printf("    sqlite3   %s\n", SQLITE_VERSION);
#endif

#ifdef WITH_INIH
    /* inih has no official version macro in older releases */
    printf("    inih      Ben Hoyt (embedded)\n");
#endif

#ifdef WITH_GETOPT
    printf("    getopt    portable + getopt_long\n");
#endif

#ifdef WITH_EZXML
    printf("    ezxml     Aaron Voisine\n");
#endif

#if !defined(WITH_SQLITE) && !defined(WITH_INIH) && !defined(WITH_GETOPT) && !defined(WITH_EZXML)
    printf("    (none)\n");
#endif
    printf("========================================\n");
}





/* ---- Protected library (Intrinsic) calls ----
 * Report SEGV/BUS inside libc in the same style as ProgramFail:
 *   <source line>
 *   <spaces>^
 *   file:line:col SEGV inside library function 'name'
 * No per-call sigsetjmp (too expensive on 100k sprintf). Unix handler
 * uses write(2) only (async-signal-safe) then _exit(1).
 */

/* Basename of a path; signal-safe (no malloc). */
static const char *PicocPathBasename(const char *file)
{
    const char *base = file ? file : "?";
    const char *s;
    for (s = base; *s; s++)
        if (*s == '/' || *s == '\\')
            base = s + 1;
    return base;
}

#ifdef UNIX_HOST
#include <unistd.h>
#endif

/* Print one source line + caret to fd, signal-safe (write only).
 * Mirrors PrintSourceTextErrorLine layout without libc buffered IO. */
#if defined(UNIX_HOST)
/* Consume write(2) result so GCC -Wunused-result is quiet without changing
 * signal-handler semantics (best-effort diagnostics before _exit). */
static void PicocWriteAll(int fd, const void *buf, size_t len)
{
    const char *p = (const char *)buf;
    while (len > 0) {
        ssize_t n = write(fd, p, len);
        if (n <= 0)
            return;
        p += (size_t)n;
        len -= (size_t)n;
    }
}

static void PicocWriteSourceCaret(int fd, const char *SourceText, int Line, int CharacterPos)
{
    const char *LinePos;
    const char *CPos;
    int LineCount;
    int CCount;
    char ch;

    if (SourceText == NULL || Line < 1)
        return;
    for (LinePos = SourceText, LineCount = 1; *LinePos != '\0' && LineCount < Line; LinePos++)
        if (*LinePos == '\n')
            LineCount++;
    /* source line */
    for (CPos = LinePos; *CPos != '\n' && *CPos != '\0'; CPos++) {
        ch = *CPos;
        PicocWriteAll(fd, &ch, 1);
    }
    ch = '\n';
    PicocWriteAll(fd, &ch, 1);
    /* spaces/tabs up to caret */
    for (CPos = LinePos, CCount = 0;
         *CPos != '\n' && *CPos != '\0' && (CCount < CharacterPos || *CPos == ' ');
         CPos++, CCount++)
    {
        ch = (*CPos == '\t') ? '\t' : ' ';
        PicocWriteAll(fd, &ch, 1);
    }
    ch = '^';
    PicocWriteAll(fd, &ch, 1);
    ch = '\n';
    PicocWriteAll(fd, &ch, 1);
}
#endif /* UNIX_HOST */

#ifdef UNIX_HOST
#include <signal.h>

static volatile sig_atomic_t PicocInIntrinsic;
static struct ParseState *PicocIntrParser;
static const char *PicocIntrName;
static struct sigaction PicocOldSegv, PicocOldBus, PicocOldIll, PicocOldFpe;
static int PicocHandlersReady;

static void PicocIntrinsicSignalHandler(int sig)
{
    const char *sn = "SIGNAL";
    char buf[384];
    int n = 0;
    if (sig == SIGSEGV) sn = "SEGV";
    else if (sig == SIGBUS) sn = "BUS";
    else if (sig == SIGILL) sn = "ILL";
    else if (sig == SIGFPE) sn = "FPE";
    if (PicocInIntrinsic) {
        const char *file = (PicocIntrParser && PicocIntrParser->FileName)
            ? PicocIntrParser->FileName : "?";
        const char *base = PicocPathBasename(file);
        int line = PicocIntrParser ? PicocIntrParser->Line : 0;
        int col = PicocIntrParser ? PicocIntrParser->CharacterPos : 0;
        const char *fn = PicocIntrName ? PicocIntrName : "?";
        const char *src = (PicocIntrParser && PicocIntrParser->SourceText)
            ? PicocIntrParser->SourceText : NULL;
        /* same visual shape as ProgramFail */
        PicocWriteSourceCaret(STDERR_FILENO, src, line, col);
        n = snprintf(buf, sizeof(buf),
            "%s:%d:%d %s inside library function '%s'\n", base, line, col, sn, fn);
        if (n > 0)
            PicocWriteAll(STDERR_FILENO, buf, (size_t)n);
        _exit(1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void PicocEnsureCrashHandlers(void)
{
    struct sigaction sa;
    if (PicocHandlersReady)
        return;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = PicocIntrinsicSignalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGSEGV, &sa, &PicocOldSegv);
    sigaction(SIGBUS, &sa, &PicocOldBus);
    sigaction(SIGILL, &sa, &PicocOldIll);
    sigaction(SIGFPE, &sa, &PicocOldFpe);
    PicocHandlersReady = 1;
}

void PicocCallIntrinsic(struct ParseState *Parser,
    void (*Intrinsic)(struct ParseState *, struct Value *, struct Value **, int),
    struct Value *ReturnValue, struct Value **Param, int NumArgs,
    const char *FuncName)
{
    if (Intrinsic == NULL)
        return;
    PicocEnsureCrashHandlers();
    /* Flush stdout so prior printf is not lost if Intrinsic SEGV + _exit. */
    fflush(stdout);
    PicocIntrParser = Parser;
    PicocIntrName = FuncName;
    PicocInIntrinsic = 1;
    Intrinsic(Parser, ReturnValue, Param, NumArgs);
    PicocInIntrinsic = 0;
    PicocIntrParser = NULL;
    PicocIntrName = NULL;
}
#elif defined(WIN32)
/* Windows: SEH around Intrinsic so AV inside libc reports in ProgramFail style. */
# if defined(_MSC_VER)
#  include <windows.h>
#  include <stdio.h>
void PicocCallIntrinsic(struct ParseState *Parser,
    void (*Intrinsic)(struct ParseState *, struct Value *, struct Value **, int),
    struct Value *ReturnValue, struct Value **Param, int NumArgs,
    const char *FuncName)
{
    if (Intrinsic == NULL)
        return;
    __try {
        Intrinsic(Parser, ReturnValue, Param, NumArgs);
    }
    __except (GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION
              || GetExceptionCode() == EXCEPTION_ILLEGAL_INSTRUCTION
              || GetExceptionCode() == EXCEPTION_INT_DIVIDE_BY_ZERO
              ? EXCEPTION_EXECUTE_HANDLER
              : EXCEPTION_CONTINUE_SEARCH)
    {
        const char *file = (Parser && Parser->FileName) ? Parser->FileName : "?";
        const char *base = PicocPathBasename(file);
        int line = Parser ? Parser->Line : 0;
        int col = Parser ? Parser->CharacterPos : 0;
        const char *fn = FuncName ? FuncName : "?";
        const char *sn = "SEGV";
        DWORD code = GetExceptionCode();
        if (code == EXCEPTION_ILLEGAL_INSTRUCTION) sn = "ILL";
        else if (code == EXCEPTION_INT_DIVIDE_BY_ZERO) sn = "FPE";
        /* full ProgramFail-style line + caret when source is available */
        if (Parser && Parser->SourceText)
            PrintSourceTextErrorLine(stderr, file, Parser->SourceText, line, col);
        else
            fprintf(stderr, "^\n%s:%d:%d ", base, line, col);
        fprintf(stderr, "%s inside library function '%s'\n", sn, fn);
        fflush(stderr);
        ExitProcess(1);
    }
}
# else
/* MinGW / non-MSVC Windows: signal() catches AV-ish crashes in Intrinsic. */
#  include <signal.h>
#  include <stdio.h>
#  include <string.h>
static volatile int PicocInIntrinsicW;
static struct ParseState *PicocIntrParserW;
static const char *PicocIntrNameW;
static int PicocHandlersReadyW;

static void PicocIntrinsicSignalHandlerW(int sig)
{
    const char *sn = "SEGV";
    if (sig == SIGILL) sn = "ILL";
    else if (sig == SIGFPE) sn = "FPE";
    if (PicocInIntrinsicW) {
        const char *file = (PicocIntrParserW && PicocIntrParserW->FileName)
            ? PicocIntrParserW->FileName : "?";
        const char *base = PicocPathBasename(file);
        int line = PicocIntrParserW ? PicocIntrParserW->Line : 0;
        int col = PicocIntrParserW ? PicocIntrParserW->CharacterPos : 0;
        const char *fn = PicocIntrNameW ? PicocIntrNameW : "?";
        fflush(stdout);
        if (PicocIntrParserW && PicocIntrParserW->SourceText)
            PrintSourceTextErrorLine(stderr, file, PicocIntrParserW->SourceText, line, col);
        fprintf(stderr, "%s:%d:%d %s inside library function '%s'\n",
            base, line, col, sn, fn);
        fflush(stderr);
        exit(1);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

static void PicocEnsureCrashHandlersW(void)
{
    if (PicocHandlersReadyW)
        return;
    signal(SIGSEGV, PicocIntrinsicSignalHandlerW);
    signal(SIGILL, PicocIntrinsicSignalHandlerW);
    signal(SIGFPE, PicocIntrinsicSignalHandlerW);
    PicocHandlersReadyW = 1;
}

void PicocCallIntrinsic(struct ParseState *Parser,
    void (*Intrinsic)(struct ParseState *, struct Value *, struct Value **, int),
    struct Value *ReturnValue, struct Value **Param, int NumArgs,
    const char *FuncName)
{
    if (Intrinsic == NULL)
        return;
    PicocEnsureCrashHandlersW();
    fflush(stdout);
    PicocIntrParserW = Parser;
    PicocIntrNameW = FuncName;
    PicocInIntrinsicW = 1;
    Intrinsic(Parser, ReturnValue, Param, NumArgs);
    PicocInIntrinsicW = 0;
    PicocIntrParserW = NULL;
    PicocIntrNameW = NULL;
}
# endif
#else
void PicocCallIntrinsic(struct ParseState *Parser,
    void (*Intrinsic)(struct ParseState *, struct Value *, struct Value **, int),
    struct Value *ReturnValue, struct Value **Param, int NumArgs,
    const char *FuncName)
{
    (void)FuncName;
    if (Intrinsic != NULL)
        Intrinsic(Parser, ReturnValue, Param, NumArgs);
}
#endif
