/* picoc main program - this varies depending on your operating system and
 * how you're using picoc */
 
/* include only picoc.h here - should be able to use it with only the external interfaces, no internals from interpreter.h */
#include "picoc.h"
#include "ir.h"

/* platform-dependent code for running programs is in this file */

#if defined(UNIX_HOST) || defined(WIN32)
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* IR compile counters + analyze log (defined in ir_compile.c) */
extern int PicocIrBodyCompiled;
extern int PicocIrBodyTokenFallback;
void BabycAnalyzeReset(void);
int BabycAnalyzeGetCount(void);
void BabycAnalyzeGet(int i, const char **Name, const char **File, int *Line, int *IsIr);
void BabycAnalyzeGetWhy(int i, const char **Why);
#ifdef WIN32
# include <windows.h>
# ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#  define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
# endif
/* Enable VT/ANSI on Windows console. No-op / fail soft if embedded or no console. */
static void PicocEnableConsoleANSI(void)
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (hOut == NULL || hOut == INVALID_HANDLE_VALUE)
        return;
    if (!GetConsoleMode(hOut, &mode))
        return;
    mode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(hOut, mode);
}
#else
static void PicocEnableConsoleANSI(void) { /* POSIX terminals usually already support ANSI */ }
#endif

#define PICOC_STACK_SIZE (2*1024*1024)             /* space for the the stack */

static void PrintUsage(const char *prog)
{
    printf("Format: %s <csource1.c>... [- <arg1>...]    : run a program (calls main() to start it)\n"
           "        %s -s <csource1.c>... [- <arg1>...] : script mode - runs without calling main()\n"
           "        %s -i                               : interactive mode\n"
           "        %s -v                               : print version / libraries banner\n"
           "        %s -c, --color                      : enable ANSI colors on console\n"
           "        %s --analyze <csource.c>...         : parse/compile IR only, no execution\n"
           "        %s --ir-stats                       : print IR compile stats on exit (or BABYC_IR_STATS=1)\n"
           "        %s -h                               : this help\n",
           prog, prog, prog, prog, prog, prog, prog, prog);
}

#ifdef PERSOPORT
int picoc_main(int argc, char **argv)
#else
int main(int argc, char **argv)
#endif
{
    int ParamCount = 1;
    int DontRunMain = FALSE;
    int StackSize = getenv("STACKSIZE") ? atoi(getenv("STACKSIZE")) : PICOC_STACK_SIZE;
    Picoc pc;

    /* Register the canonical-type gate report even for parser-only and error
     * fixtures that never evaluate or emit an IR expression. */
    IrCanonicalCheckNode(NULL);
    if (argc < 2)
    {
        PrintUsage(argv[0] ? argv[0] : "picoc");
        exit(1);
    }

    /* early flags that need no interpreter state */
    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0)
    {
        PrintUsage(argv[0] ? argv[0] : "picoc");
        return 0;
    }
    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0)
    {
        PicocPrintBanner();
        return 0;
    }

    /* scan argv for early flags (can combine with -i or scripts) */
    {
        int ai;
        int AnalyzeMode = 0;
        for (ai = 1; ai < argc; ai++) {
            if (strcmp(argv[ai], "-c") == 0 || strcmp(argv[ai], "--color") == 0) {
                PicocEnableConsoleANSI();
            } else if (strcmp(argv[ai], "--ir-stats") == 0) {
                /* force stats on exit (also: env BABYC_IR_STATS=1) */
#ifdef _WIN32
                _putenv("BABYC_IR_STATS=1");
#else
                setenv("BABYC_IR_STATS", "1", 1);
#endif
            } else if (strcmp(argv[ai], "--analyze") == 0) {
                AnalyzeMode = 1;
            }
        }
        if (AnalyzeMode) {
            /* parse & compile only — never call main / never run statements */
            DontRunMain = TRUE;
        }
    }
    
    PicocInitialise(&pc, StackSize);

    if (PicocPlatformSetExitPoint(&pc))
    {
        PicocPrintIrStats(); LexPrintStats(); PicocCleanup(&pc);
        return pc.PicocExitValue;
    }
    
    if (strcmp(argv[ParamCount], "-s") == 0 || strcmp(argv[ParamCount], "-m") == 0)
    {
        DontRunMain = TRUE;
        PicocIncludeAllSystemHeaders(&pc);
        ParamCount++;
    }

    /* --analyze: no main, no forced IncludeAll (file #includes pull what they need).
     * Avoids known size_t redef noise from IncludeAll + unistd on some hosts. */
    /* DontRunMain already set in early-flag scan when --analyze present */
        
    if (argc > ParamCount && strcmp(argv[ParamCount], "-i") == 0)
    {
        PicocIncludeAllSystemHeaders(&pc);
        PicocPrintBanner();
        PicocParseInteractive(&pc);
    }
    else
    {
        for (; ParamCount < argc && strcmp(argv[ParamCount], "-") != 0; ParamCount++) {
            if (strcmp(argv[ParamCount], "-c") == 0 || strcmp(argv[ParamCount], "--color") == 0)
                continue; /* already handled */
            if (strcmp(argv[ParamCount], "--ir-stats") == 0 ||
                strcmp(argv[ParamCount], "--analyze") == 0)
                continue;
            if (strcmp(argv[ParamCount], "-i") == 0 || strcmp(argv[ParamCount], "-v") == 0 ||
                strcmp(argv[ParamCount], "-h") == 0)
                continue;
            PicocPlatformScanFile(&pc, argv[ParamCount]);
        }
        
        if (!DontRunMain)
            PicocCallMain(&pc, argc - ParamCount, &argv[ParamCount]);
    }
    
    /* --analyze report (after parse/compile, before cleanup) */
    {
        int ai, isAnalyze = 0;
        for (ai = 1; ai < argc; ai++) {
            if (strcmp(argv[ai], "--analyze") == 0) { isAnalyze = 1; break; }
        }
        if (isAnalyze) {
            int t = PicocIrBodyCompiled + PicocIrBodyTokenFallback;
            int ai2, n = BabycAnalyzeGetCount();
            printf("=== babyc analyze ===\n");
            for (ai2 = 0; ai2 < n; ai2++) {
                const char *nm, *fl;
                int ln, isir;
                BabycAnalyzeGet(ai2, &nm, &fl, &ln, &isir);
                /* basename only for expect stability */
                {
                    const char *base = fl;
                    const char *p;
                    const char *why = "";
                    for (p = fl; *p; p++)
                        if (*p == '/' || *p == '\\') base = p + 1;
                    if (!isir) BabycAnalyzeGetWhy(ai2, &why);
                    if (!isir && why && why[0])
                        printf("  %-5s %s  (%s:%d)  why=%s\n", "tok", nm, base, ln, why);
                    else
                        printf("  %-5s %s  (%s:%d)\n", isir ? "IR" : "tok", nm, base, ln);
                }
            }
            printf("---\n");
            printf("user functions: %d  BodyIR: %d  token-fallback: %d  (%.0f%% IR)\n",
                   t, PicocIrBodyCompiled, PicocIrBodyTokenFallback,
                   t > 0 ? (100.0 * PicocIrBodyCompiled / t) : 0.0);
            printf("compile counters: ir=%d token=%d\n",
                   PicocIrBodyCompiled, PicocIrBodyTokenFallback);
            printf("=== end analyze ===\n");
        }
    }

    PicocPrintIrStats(); LexPrintStats(); PicocCleanup(&pc);
    return pc.PicocExitValue;
}
#else
# ifdef SURVEYOR_HOST
#  define HEAP_SIZE C_HEAPSIZE
#  include <setjmp.h>
#  include "../srv.h"
#  include "../print.h"
#  include "../string.h"

int picoc(char *SourceStr)
{   
    char *pos;

    PicocInitialise(HEAP_SIZE);

    if (SourceStr)
    {
        for (pos = SourceStr; *pos != 0; pos++)
        {
            if (*pos == 0x1a)
            {
                *pos = 0x20;
            }
        }
    }

    PicocExitBuf[40] = 0;
    PicocPlatformSetExitPoint();
    if (PicocExitBuf[40]) {
        printf("Leaving PicoC\n\r");
        PicocCleanup();
        return PicocExitValue;
    }

    if (SourceStr)   
        PicocParse("nofile", SourceStr, strlen(SourceStr), TRUE, TRUE, FALSE);

    PicocParseInteractive();
    PicocCleanup();
    
    return PicocExitValue;
}
# endif
#endif
