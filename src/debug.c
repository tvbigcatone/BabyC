/* picoc interactive debugger */

#ifndef NO_DEBUGGER

#include "interpreter.h"

/* Hash only on FileName + Line so we can set breakpoints by line number
 * without knowing the exact character position. */
#define BREAKPOINT_HASH_LINE(fn, line) ( ((unsigned long)(fn)) ^ ((unsigned long)(line) << 16) )

/* initialise the debugger by clearing the breakpoint table */
void DebugInit(Picoc *pc)
{
    TableInitTable(&pc->BreakpointTable, &pc->BreakpointHashTable[0], BREAKPOINT_TABLE_SIZE, TRUE);
    pc->BreakpointCount = 0;
}

/* free the contents of the breakpoint table */
void DebugCleanup(Picoc *pc)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;
    
    for (Count = 0; Count < pc->BreakpointTable.Size; Count++)
    {
        for (Entry = pc->BreakpointHashTable[Count]; Entry != NULL; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            HeapFreeMem(pc, Entry);
        }
    }
}

/* search the table for a breakpoint matching FileName + Line
 * (CharacterPos is ignored so that a breakpoint set on a line fires
 * for any statement on that line). */
static struct TableEntry *DebugTableSearchBreakpoint(struct ParseState *Parser, int *AddAt)
{
    struct TableEntry *Entry;
    Picoc *pc = Parser->pc;
    int HashValue = BREAKPOINT_HASH_LINE(Parser->FileName, Parser->Line) % pc->BreakpointTable.Size;
    
    for (Entry = pc->BreakpointHashTable[HashValue]; Entry != NULL; Entry = Entry->Next)
    {
        if (Entry->p.b.FileName == Parser->FileName && Entry->p.b.Line == Parser->Line)
            return Entry;   /* found */
    }
    
    *AddAt = HashValue;    /* didn't find it in the chain */
    return NULL;
}

/* set a breakpoint at the current parser position (FileName + Line) */
void DebugSetBreakpoint(struct ParseState *Parser)
{
    int AddAt;
    struct TableEntry *FoundEntry = DebugTableSearchBreakpoint(Parser, &AddAt);
    Picoc *pc = Parser->pc;
    
    if (FoundEntry == NULL)
    {   
        /* add it to the table */
        struct TableEntry *NewEntry = HeapAllocMem(pc, sizeof(struct TableEntry));
        if (NewEntry == NULL)
            ProgramFailNoParser(pc, DiagnosticMsgOutOfMemory);
            
        NewEntry->p.b.FileName = Parser->FileName;
        NewEntry->p.b.Line = Parser->Line;
        NewEntry->p.b.CharacterPos = 0;   /* ignored for matching */
        NewEntry->Next = pc->BreakpointHashTable[AddAt];
        pc->BreakpointHashTable[AddAt] = NewEntry;
        pc->BreakpointCount++;
    }
}

/* set a breakpoint on a specific line of a given file */
void DebugSetBreakpointAtLine(Picoc *pc, const char *FileName, int Line)
{
    struct ParseState FakeParser;
    memset(&FakeParser, 0, sizeof(FakeParser));
    FakeParser.pc = pc;
    FakeParser.FileName = (char *)FileName;
    FakeParser.Line = (short)Line;
    FakeParser.CharacterPos = 0;
    DebugSetBreakpoint(&FakeParser);
}

/* delete a breakpoint from the hash table (by current parser position) */
int DebugClearBreakpoint(struct ParseState *Parser)
{
    struct TableEntry **EntryPtr;
    Picoc *pc = Parser->pc;
    int HashValue = BREAKPOINT_HASH_LINE(Parser->FileName, Parser->Line) % pc->BreakpointTable.Size;
    
    for (EntryPtr = &pc->BreakpointHashTable[HashValue]; *EntryPtr != NULL; EntryPtr = &(*EntryPtr)->Next)
    {
        struct TableEntry *DeleteEntry = *EntryPtr;
        if (DeleteEntry->p.b.FileName == Parser->FileName && DeleteEntry->p.b.Line == Parser->Line)
        {
            *EntryPtr = DeleteEntry->Next;
            HeapFreeMem(pc, DeleteEntry);
            pc->BreakpointCount--;
            return TRUE;
        }
    }

    return FALSE;
}

/* clear all breakpoints */
void DebugClearAllBreakpoints(Picoc *pc)
{
    struct TableEntry *Entry;
    struct TableEntry *NextEntry;
    int Count;
    
    for (Count = 0; Count < pc->BreakpointTable.Size; Count++)
    {
        for (Entry = pc->BreakpointHashTable[Count]; Entry != NULL; Entry = NextEntry)
        {
            NextEntry = Entry->Next;
            HeapFreeMem(pc, Entry);
        }
        pc->BreakpointHashTable[Count] = NULL;
    }
    pc->BreakpointCount = 0;
}

/* list all breakpoints */
void DebugListBreakpoints(Picoc *pc)
{
    struct TableEntry *Entry;
    int Count;
    int shown = 0;
    
    PlatformPrintf(pc->CStdOut, "Breakpoints (%d):\n", pc->BreakpointCount);
    for (Count = 0; Count < pc->BreakpointTable.Size; Count++)
    {
        for (Entry = pc->BreakpointHashTable[Count]; Entry != NULL; Entry = Entry->Next)
        {
            PlatformPrintf(pc->CStdOut, "  %s:%d\n",
                Entry->p.b.FileName ? Entry->p.b.FileName : "(interactive)",
                Entry->p.b.Line);
            shown++;
        }
    }
    if (shown == 0)
        PlatformPrintf(pc->CStdOut, "  (none)\n");
}

/* before we run a statement, check if there's anything we have to do with the debugger here */
void DebugCheckStatement(struct ParseState *Parser)
{
    int DoBreak = FALSE;
    int AddAt;
    Picoc *pc = Parser->pc;
    
    /* has the user manually pressed break? */
    if (pc->DebugManualBreak)
    {
        PlatformPrintf(pc->CStdOut, "Manual break\n");
        DoBreak = TRUE;
        pc->DebugManualBreak = FALSE;
    }

    /* single-step mode: break on every statement */
    if (pc->DebugStepMode)
    {
        DoBreak = TRUE;
        pc->DebugStepMode = 0;   /* consume one step */
    }
    
    /* is this a breakpoint location? */
    if (pc->BreakpointCount != 0 && DebugTableSearchBreakpoint(Parser, &AddAt) != NULL)
        DoBreak = TRUE;
    
    /* handle a break */
    if (DoBreak)
    {
        PlatformPrintf(pc->CStdOut, "\n*** Break at %s:%d ***\n",
            Parser->FileName ? Parser->FileName : "(interactive)",
            Parser->Line);
        if (Parser->SourceText != NULL)
            PrintSourceTextErrorLine(pc->CStdOut, Parser->FileName, Parser->SourceText, Parser->Line, Parser->CharacterPos);
        PlatformPrintf(pc->CStdOut,
            "  (Enter)=continue   step/s=next stmt   print <expr>\n"
            "  list_breakpoints   set_breakpoint <line>   clear_breakpoints\n");
        PicocParseInteractiveNoStartPrompt(pc, FALSE);
    }
}

/* Request a single-step: the next statement will stop in the debugger. */
void DebugStep(Picoc *pc)
{
    if (pc != NULL)
        pc->DebugStepMode = 1;
}
#endif /* !NO_DEBUGGER */
