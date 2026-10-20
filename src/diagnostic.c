/* diagnostic.c - engine-neutral diagnostic delivery.
 *
 * Execution engines supply a source location and message; this module owns
 * the presentation. It is deliberately off the successful execution path. */
#include "picoc.h"
#include "interpreter.h"
#include <string.h>
#include <stdint.h>

/* Canonical message catalog for diagnostics shared by execution engines.
 * Keep the literal roots here so source audits can identify any new engine
 * that bypasses the common diagnostic vocabulary. */
const char DiagnosticMsgNullPointerDereference[] = "NULL pointer dereference";
const char DiagnosticMsgUninitializedVariable[] = "uninitialized variable";
const char DiagnosticMsgUninitializedVariableNamed[] = "uninitialized variable '%s'";
const char DiagnosticMsgDivisionByZero[] = "division by zero";
const char DiagnosticMsgModuloByZero[] = "modulo by zero";
const char DiagnosticMsgArrayIndexOutOfBounds[] = "array index out of bounds";
const char DiagnosticMsgArrayIndexOutOfBoundsSized[] = "array index %d out of bounds (size %d)";
const char DiagnosticMsgOutOfMemory[] = "out of memory";
const char DiagnosticMsgNullFunctionPointer[] = "null function pointer";
const char DiagnosticMsgFunctionUndefined[] = "function is undefined";
const char DiagnosticMsgMemcpyOverlap[] = "memcpy source and destination regions overlap (undefined behavior in C)";

/* Compare ranges by distance rather than end-address addition, which could
 * wrap uintptr_t for hostile or malformed pointer values. Zero-byte copies
 * access no bytes and therefore have no overlapping byte ranges. */
int PicocMemoryRangesOverlap(const void *A, const void *B, size_t Bytes)
{
    uintptr_t X, Y;
    if (Bytes == 0 || A == NULL || B == NULL) return 0;
    X = (uintptr_t)A;
    Y = (uintptr_t)B;
    return X <= Y ? (Y - X) < Bytes : (X - Y) < Bytes;
}

/* Error-path-only source access resolver. Runtime engines often detect an
 * uninitialized aggregate at its storage root (v) or after evaluating an
 * indexed aggregate (v[i].field). Normalize that legacy anchor here, once,
 * instead of teaching every engine to rediscover the same member column.
 * This is deliberately not used by successful execution and adds no VM
 * instruction fields or per-instruction bookkeeping. */
static int DiagnosticMemberColumn(const char *SourceText, int Line, int Column)
{
    const char *LP, *P;
    int L = 1, BestCol = 0, BestDistance = 0x7fffffff;
    if (SourceText == NULL || Line <= 0) return 0;
    for (LP = SourceText; *LP != '\0' && L < Line; LP++)
        if (*LP == '\n') L++;
    if (L != Line) return 0;

    /* Ignore strings and comments while scanning this source line. */
    {
        int InString = 0, InChar = 0, Escape = 0;
        for (P = LP; *P != '\0' && *P != '\n' && *P != '\r'; ) {
            const char *Start, *Q, *LastMember = NULL;
            int HasMember = 0, EndCol, StartCol, Distance;
            char C = *P;
            if (Escape) { Escape = 0; P++; continue; }
            if ((InString || InChar) && C == '\\') { Escape = 1; P++; continue; }
            if (!InChar && C == '"') { InString = !InString; P++; continue; }
            if (!InString && C == '\'') { InChar = !InChar; P++; continue; }
            if (InString || InChar) { P++; continue; }
            if (C == '/' && P[1] == '/') break;
            if (!((C >= 'a' && C <= 'z') || (C >= 'A' && C <= 'Z') || C == '_')) {
                P++; continue;
            }
            Start = P;
            Q = P + 1;
            while ((*Q >= 'a' && *Q <= 'z') || (*Q >= 'A' && *Q <= 'Z')
                   || (*Q >= '0' && *Q <= '9') || *Q == '_') Q++;
            P = Q;
            for (;;) {
                while (*P == ' ' || *P == '\t') P++;
                if (*P == '[') {
                    int Depth = 1, InS = 0, InC = 0, Esc = 0;
                    P++;
                    while (*P && *P != '\n' && *P != '\r' && Depth > 0) {
                        if (Esc) { Esc = 0; P++; continue; }
                        if ((InS || InC) && *P == '\\') { Esc = 1; P++; continue; }
                        if (!InC && *P == '"') InS = !InS;
                        else if (!InS && *P == '\'') InC = !InC;
                        else if (!InS && !InC && *P == '[') Depth++;
                        else if (!InS && !InC && *P == ']') Depth--;
                        P++;
                    }
                    if (Depth != 0) break;
                    continue;
                }
                if (*P == '.' || (P[0] == '-' && P[1] == '>')) {
                    P += (*P == '.') ? 1 : 2;
                    while (*P == ' ' || *P == '\t') P++;
                    if (!((*P >= 'a' && *P <= 'z') || (*P >= 'A' && *P <= 'Z') || *P == '_')) break;
                    LastMember = P;
                    HasMember = 1;
                    P++;
                    while ((*P >= 'a' && *P <= 'z') || (*P >= 'A' && *P <= 'Z')
                           || (*P >= '0' && *P <= '9') || *P == '_') P++;
                    continue;
                }
                break;
            }
            if (HasMember && LastMember != NULL) {
                StartCol = (int)(Start - LP) + 1;
                EndCol = (int)(P - LP) + 1;
                if (Column < StartCol) Distance = StartCol - Column;
                else if (Column > EndCol) Distance = Column - EndCol;
                else Distance = 0;
                if (Distance < BestDistance) {
                    BestDistance = Distance;
                    BestCol = (int)(LastMember - LP) + 1;
                }
            }
            /* The outer scan resumes at the end of the access expression. */
        }
    }
    return BestCol;
}

static int DiagnosticResolvedColumn(const DiagnosticLocation *Location,
                                    const char *Message, int Fallback)
{
    int Column = Fallback;
    if (Location != NULL && Message != NULL
        && strstr(Message, DiagnosticMsgUninitializedVariable) != NULL) {
        int MemberColumn = DiagnosticMemberColumn(Location->SourceText,
                                                   Location->Line, Fallback);
        if (MemberColumn > 0) Column = MemberColumn;
    }
    return Column;
}

static void DiagnosticRenderLocation(Picoc *pc, const DiagnosticLocation *Location,
                                     const char *Message)
{
    IOFILE *Stream = (pc != NULL) ? pc->CStdOut : NULL;
    if (Stream == NULL || Location == NULL || Location->SourceText == NULL
        || Location->Line <= 0)
        return;

    /* Preserve Column==0 (historical parse-error / unknown-column convention).
     * Only member-column resolution may replace a positive fallback. */
    {
        int Column = Location->Column;
        if (Column > 0)
            Column = DiagnosticResolvedColumn(Location, Message, Column);
        if (Column < 0)
            Column = 0;
        PrintSourceTextErrorLine(Stream, Location->FileName, Location->SourceText,
                                 Location->Line, Column);
    }
}

void DiagnosticReportV(Picoc *pc, const DiagnosticLocation *Location,
                       const char *Message, va_list Args)
{
    IOFILE *Stream = (pc != NULL) ? pc->CStdOut : NULL;

    if (Stream == NULL)
    {
        vfprintf(stderr, Message, Args);
        fputc('\n', stderr);
        fflush(stderr);
        return;
    }

    DiagnosticRenderLocation(pc, Location, Message);
    PlatformVPrintf(Stream, Message, Args);
    PlatformPrintf(Stream, "\n");
    fflush(Stream);
}

void DiagnosticReportAssign(Picoc *pc, const DiagnosticLocation *Location,
                            const char *Format, struct ValueType *Type1,
                            struct ValueType *Type2, int Num1, int Num2,
                            const char *FuncName, int ParamNo)
{
    IOFILE *Stream = (pc != NULL) ? pc->CStdOut : NULL;
    if (Stream == NULL)
    {
        fprintf(stderr, "can't %s\n", (FuncName == NULL) ? "assign" : "set");
        fflush(stderr);
        return;
    }

    DiagnosticRenderLocation(pc, Location, "");
    PlatformPrintf(Stream, "can't %s ", (FuncName == NULL) ? "assign" : "set");
    if (Type1 != NULL)
        PlatformPrintf(Stream, Format, Type1, Type2);
    else
        PlatformPrintf(Stream, Format, Num1, Num2);
    if (FuncName != NULL)
        PlatformPrintf(Stream, " in argument %d of call to %s()", ParamNo, (char *)FuncName);
    PlatformPrintf(Stream, "\n");
    fflush(Stream);
}

void DiagnosticReport(Picoc *pc, const DiagnosticLocation *Location,
                     const char *Message, ...)
{
    va_list Args;
    va_start(Args, Message);
    DiagnosticReportV(pc, Location, Message, Args);
    va_end(Args);
}

int ParserSetDiagnosticCaret(struct ParseState *Parser, const char *BaseIdent,
                             const char *MemberIdent)
{
    const char *Src, *LinePos, *CPos, *Base = NULL, *Dot = NULL;
    int LineCount, BaseLen, MemberLen;
    if (Parser == NULL || Parser->SourceText == NULL)
        return 0;
    Src = Parser->SourceText;
    for (LinePos = Src, LineCount = 1;
         *LinePos != '\0' && LineCount < Parser->Line; LinePos++)
        if (*LinePos == '\n') LineCount++;
    if (*LinePos == '\r') LinePos++;
    BaseLen = (BaseIdent != NULL) ? (int)strlen(BaseIdent) : 0;
    MemberLen = (MemberIdent != NULL) ? (int)strlen(MemberIdent) : 0;

    /* Find the last whole-word root identifier on this source line. */
    if (BaseLen > 0) {
        for (CPos = LinePos; *CPos && *CPos != '\n' && *CPos != '\r'; CPos++) {
            char before, after;
            if (strncmp(CPos, BaseIdent, (size_t)BaseLen) != 0) continue;
            before = CPos > LinePos ? CPos[-1] : ' ';
            after = CPos[BaseLen];
            if ((before >= 'a' && before <= 'z') || (before >= 'A' && before <= 'Z')
                || before == '_' || (before >= '0' && before <= '9')) continue;
            if ((after >= 'a' && after <= 'z') || (after >= 'A' && after <= 'Z')
                || after == '_' || (after >= '0' && after <= '9')) continue;
            Base = CPos;
        }
    }
    if (Base != NULL) {
        const char *End = Base;
        while (*End && *End != '\n' && *End != '\r' && *End != ';' && *End != ',') End++;
        /* A named member is preferred only when it belongs to this access. */
        if (MemberLen > 0) {
            for (CPos = Base + BaseLen; CPos + MemberLen <= End; CPos++) {
                if (strncmp(CPos, MemberIdent, (size_t)MemberLen) == 0) {
                    char before = CPos > LinePos ? CPos[-1] : ' ';
                    char after = CPos[MemberLen];
                    if (before == '.' && !((after >= 'a' && after <= 'z')
                        || (after >= 'A' && after <= 'Z') || after == '_'
                        || (after >= '0' && after <= '9'))) {
                        Parser->CharacterPos = (int)(CPos - LinePos) + 1;
                        return 1;
                    }
                }
            }
        }
        /* Otherwise anchor the last .member leaf after the root, allowing
         * intervening indexes and nested member access. */
        for (CPos = Base + BaseLen; CPos < End; CPos++)
            if (*CPos == '.' && CPos[1] != '\0' && CPos[1] != '.'
                && ((CPos[1] >= 'a' && CPos[1] <= 'z')
                    || (CPos[1] >= 'A' && CPos[1] <= 'Z') || CPos[1] == '_'))
                Dot = CPos + 1;
        if (Dot != NULL) {
            Parser->CharacterPos = (int)(Dot - LinePos) + 1;
            return 1;
        }
    }
    if (BaseIdent != NULL)
        ParserSetCaretOnIdent(Parser, BaseIdent);
    return 0;
}

