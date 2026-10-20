/* ir_stubs.c — no-op IR surface for Makefile.minimal (PIANO_TOKEN_PATH_V2 P2) */
#include "interpreter.h"
#include "ir.h"
#include "ir_runtime.h"
#include "ir_private.h"
#include <stddef.h>
#include <string.h>

int PicocIrBodyCompiled;
int PicocIrBodyTokenFallback;

void IrFreeFunctionBody(void *BodyIr) { (void)BodyIr; }
struct IrStmt *IrCompileBody(struct ParseState *BodyStart, struct ParseState *BodyEnd)
{ (void)BodyStart; (void)BodyEnd; return NULL; }
void IrCompileSetParamContext(char **ParamNames, struct ValueType **ParamTypes, int NumParams)
{ (void)ParamNames; (void)ParamTypes; (void)NumParams; }
void IrCompileClearParamContext(void) {}
void *IrCompileFunctionBody(struct ParseState *BodyStart)
{ (void)BodyStart; return NULL; }

void BabycAnalyzeReset(void) {}
int BabycAnalyzeGetCount(void) { return 0; }
void BabycAnalyzeGet(int i, const char **Name, const char **File, int *Line, int *IsIr)
{ (void)i; if (Name) *Name = NULL; if (File) *File = NULL; if (Line) *Line = 0; if (IsIr) *IsIr = 0; }
void BabycAnalyzeGetWhy(int i, const char **Why) { (void)i; if (Why) *Why = NULL; }
void BabycAnalyzeNote(const char *Name, const char *File, int Line, int IsIr)
{ (void)Name; (void)File; (void)Line; (void)IsIr; }

int IrFrameGetByName(const char *Name, long long *Out) { (void)Name; (void)Out; return 0; }
int IrFrameSetByName(const char *Name, long long V) { (void)Name; (void)V; return 0; }

int FastForCaptureCond(struct ParseState *From, FastForCond *Out)
{ (void)From; if (Out) memset(Out, 0, sizeof(*Out)); return 0; }
int FastForCaptureInc(struct ParseState *From, FastForInc *Out)
{ (void)From; if (Out) memset(Out, 0, sizeof(*Out)); return 0; }
PTR_TYPE_INT_SIGNED FastForEvalCond(struct ParseState *Parser, FastForCond *C)
{ (void)Parser; (void)C; return 0; }

void IrCondFromFast(IrCond *Dst, FastForCond *Src) { (void)Dst; (void)Src; }
void IrIncFromFast(IrInc *Dst, FastForInc *Src) { (void)Dst; (void)Src; }
void IrExecuteLoop(struct ParseState *Parser, IrLoop *Loop, enum RunMode PreMode)
{ (void)Parser; (void)Loop; (void)PreMode; }
void LoopRunBody(struct ParseState *Parser, IrLoop *Loop, int CheckTrailingSemicolon)
{ (void)Parser; (void)Loop; (void)CheckTrailingSemicolon; }
int IrTryTightLoopVars(struct ParseState *Parser, IrLoop *Loop)
{ (void)Parser; (void)Loop; return 0; }
int TryStripTrailingInc(struct ParseState *BodyStart, struct ParseState *BodyEnd,
                        const char *VarName, FastForInc *Inc,
                        struct ParseState *NewBodyStart, struct ParseState *NewBodyEnd)
{ (void)BodyStart; (void)BodyEnd; (void)VarName; (void)Inc;
  (void)NewBodyStart; (void)NewBodyEnd; return 0; }
void IrPrintFpStats(void) {}
void IrExecuteFunctionBody(struct ParseState *Parser, struct Value *FuncValue,
                           struct Value **ParamArray, int ArgCount)
{ (void)Parser; (void)FuncValue; (void)ParamArray; (void)ArgCount; }

/* Micro: IR runtime is not linked. Safe checks stay off (previous micro). */
int IrSafeChecksOn(void) { return 0; }
int IrUninitChecksOn(void) { return 0; }
void IrPrintGlobalBindStats(void) {}
