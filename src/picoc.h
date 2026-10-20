#define TAG "BabyC"
/* picoc external interface. This should be the only header you need to use if
 * you're using picoc as a library. Internal details are in interpreter.h */
#ifndef PICOC_H
#define PICOC_H

/* picoc version number */
#ifdef VER
#define PICOC_VERSION "v2.2 beta r" VER         /* VER is the subversion version number, obtained via the Makefile */
#else
#define PICOC_VERSION "v2.2"
#endif

/* Startup banner: PicoC version + compiled third-party libraries */
void PicocPrintBanner(void);

/* handy definitions */
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif

#include "interpreter.h"


#if defined(UNIX_HOST) || defined(WIN32)
#include <setjmp.h>

/* Macro required: setjmp must be in the caller's frame.
 * Arm PicocExitPointSet on first return (0); clear on longjmp return (≠0). */
#define PicocPlatformSetExitPoint(pc) \
    ( setjmp((pc)->PicocExitBuf) \
        ? ((pc)->PicocExitPointSet = 0, 1) \
        : ((pc)->PicocExitPointSet = 1, 0) )
#endif

#ifdef SURVEYOR_HOST
/* mark where to end the program for platforms which require this */
extern int PicocExitBuf[];

#define PicocPlatformSetExitPoint(pc) \
    ( setjmp((pc)->PicocExitBuf) \
        ? ((pc)->PicocExitPointSet = 0, 1) \
        : ((pc)->PicocExitPointSet = 1, 0) )
#endif

/* parse.c */
void PicocParse(Picoc *pc, const char *FileName, const char *Source, int SourceLen, int RunIt, int CleanupNow, int CleanupSource, int EnableDebugger);
void PicocParseInteractive(Picoc *pc);

/* platform.c */
void PicocCallMain(Picoc *pc, int argc, char **argv);
void PicocInitialise(Picoc *pc, int StackSize);
void PicocCleanup(Picoc *pc);
void PicocPlatformScanFile(Picoc *pc, const char *FileName);

/* include.c */
void PicocIncludeAllSystemHeaders(Picoc *pc);

#endif /* PICOC_H */
