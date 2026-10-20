/* picoc / BabyC heap memory allocation.
 * Optimised freelist (2026-09): more size-class buckets (up to 128 B) + best-fit
 * on the big freelist to reduce fragmentation and average scan cost under IR-heavy
 * workloads (many short-lived 16-96 byte nodes).
 * Still a complete (but small) allocator for embedded systems; define
 * USE_MALLOC_HEAP to fall back to the system malloc().
 */

/* stack grows up from the bottom and heap grows down from the top of heap space */
#include "interpreter.h"
#include <limits.h>   /* INT_MAX for best-fit */

#ifdef DEBUG_HEAP
void ShowBigList(Picoc *pc)
{
    struct AllocNode *LPos;

    printf("Heap: bottom=0x%lx 0x%lx-0x%lx, big freelist=", (long)pc->HeapBottom, (long)&(pc->HeapMemory)[0], (long)&(pc->HeapMemory)[HEAP_SIZE]);
    for (LPos = pc->FreeListBig; LPos != NULL; LPos = LPos->NextFree)
        printf("0x%lx:%d ", (long)LPos, LPos->Size);

    printf("\n");
}
#endif

/* initialise the stack and heap storage */
void HeapInit(Picoc *pc, int StackOrHeapSize)
{
    int Count;
    int AlignOffset = 0;

#ifdef USE_MALLOC_STACK
    pc->HeapMemory = malloc(StackOrHeapSize);
    pc->HeapBottom = NULL;                     /* the bottom of the (downward-growing) heap */
    pc->StackFrame = NULL;                     /* the current stack frame */
    pc->HeapStackTop = NULL;                          /* the top of the stack */
#else
# ifdef SURVEYOR_HOST
    pc->HeapMemory = (unsigned char *)C_HEAPSTART;      /* all memory - stack and heap */
    pc->HeapBottom = (void *)C_HEAPSTART + HEAP_SIZE;  /* the bottom of the (downward-growing) heap */
    pc->StackFrame = (void *)C_HEAPSTART;              /* the current stack frame */
    pc->HeapStackTop = (void *)C_HEAPSTART;                   /* the top of the stack */
    pc->HeapMemStart = (void *)C_HEAPSTART;
# else
    pc->HeapBottom = &pc->HeapMemory[HEAP_SIZE];   /* the bottom of the (downward-growing) heap */
    pc->StackFrame = &pc->HeapMemory[0];           /* the current stack frame */
    pc->HeapStackTop = &pc->HeapMemory[0];                /* the top of the stack */
# endif
#endif

    while (((PTR_TYPE_INT)&pc->HeapMemory[AlignOffset] & (sizeof(ALIGN_TYPE)-1)) != 0)
        AlignOffset++;

    pc->StackFrame = &(pc->HeapMemory)[AlignOffset];
    pc->HeapStackTop = &(pc->HeapMemory)[AlignOffset];
    *(void **)(pc->StackFrame) = NULL;
    pc->HeapBottom = &(pc->HeapMemory)[StackOrHeapSize-sizeof(ALIGN_TYPE)+AlignOffset];
    pc->FreeListBig = NULL;
    for (Count = 0; Count < FREELIST_BUCKETS; Count++)
        pc->FreeListBucket[Count] = NULL;
}

void HeapCleanup(Picoc *pc)
{
#ifdef USE_MALLOC_STACK
    free(pc->HeapMemory);
#endif
}

/* allocate some space on the stack, in the current stack frame
 * clears memory. can return NULL if out of stack space */
void *HeapAllocStack(Picoc *pc, int Size)
{
    char *NewMem = pc->HeapStackTop;
    char *NewTop = (char *)pc->HeapStackTop + MEM_ALIGN(Size);
#ifdef DEBUG_HEAP
    printf("HeapAllocStack(%ld) at 0x%lx\n", (PTR_TYPE_INT)MEM_ALIGN(Size), (PTR_TYPE_INT)pc->HeapStackTop);
#endif
    if (NewTop > (char *)pc->HeapBottom)
        return NULL;

    pc->HeapStackTop = (void *)NewTop;
    memset((void *)NewMem, '\0', Size);
    return NewMem;
}

/* allocate some space on the stack, in the current stack frame */
void HeapUnpopStack(Picoc *pc, int Size)
{
#ifdef DEBUG_HEAP
    printf("HeapUnpopStack(%ld) at 0x%lx\n", (PTR_TYPE_INT)MEM_ALIGN(Size), (PTR_TYPE_INT)pc->HeapStackTop);
#endif
    pc->HeapStackTop = (void *)((char *)pc->HeapStackTop + MEM_ALIGN(Size));
}

/* free some space at the top of the stack */
int HeapPopStack(Picoc *pc, void *Addr, int Size)
{
    int ToLose = MEM_ALIGN(Size);
    char *OldTop = (char *)pc->HeapStackTop;
    struct ValueProxyEntry **Link;
    int Changed;

    /* Proxies are trailing storage on only the affected Value allocations.
     * Include their bytes when a pop crosses the owning Value, then retire the
     * metadata. Ordinary stack values retain the original accounting path. */
    do
    {
        Changed = FALSE;
        Link = &pc->ValueProxyList;
        while (*Link != NULL)
        {
            struct ValueProxyEntry *Entry = *Link;
            char *Owner = (char *)Entry->Owner;
            char *Candidate = OldTop - ToLose;
            if (Owner < OldTop
                && Owner + MEM_ALIGN(sizeof(struct Value)) + sizeof(union AnyValue) > Candidate)
            {
                ToLose += (int)sizeof(union AnyValue);
                *Link = Entry->Next;
                HeapFreeMem(pc, Entry);
                Changed = TRUE;
                break;
            }
            Link = &Entry->Next;
        }
    } while (Changed);

    if (ToLose > ((char *)pc->HeapStackTop - (char *)&(pc->HeapMemory)[0]))
        return FALSE;

#ifdef DEBUG_HEAP
    printf("HeapPopStack(0x%lx, %ld) back to 0x%lx\n", (PTR_TYPE_INT)Addr, (PTR_TYPE_INT)MEM_ALIGN(Size), (PTR_TYPE_INT)pc->HeapStackTop - ToLose);
#endif
    pc->HeapStackTop = (void *)((char *)pc->HeapStackTop - ToLose);
    assert(Addr == NULL || pc->HeapStackTop == Addr);

    return TRUE;
}

/* push a new stack frame on to the stack */
void HeapPushStackFrame(Picoc *pc)
{
#ifdef DEBUG_HEAP
    printf("Adding stack frame at 0x%lx\n", (PTR_TYPE_INT)pc->HeapStackTop);
#endif
    *(void **)pc->HeapStackTop = pc->StackFrame;
    pc->StackFrame = pc->HeapStackTop;
    pc->HeapStackTop = (void *)((char *)pc->HeapStackTop + MEM_ALIGN(sizeof(ALIGN_TYPE)));
}

/* pop the current stack frame, freeing all memory in the frame. can return NULL */
int HeapPopStackFrame(Picoc *pc)
{
    if (*(void **)pc->StackFrame != NULL)
    {
        struct ValueProxyEntry **Link = &pc->ValueProxyList;
        char *FrameTop = (char *)pc->StackFrame;
        while (*Link != NULL)
        {
            struct ValueProxyEntry *Entry = *Link;
            if ((char *)Entry->Owner >= FrameTop)
            {
                *Link = Entry->Next;
                HeapFreeMem(pc, Entry);
            }
            else
                Link = &Entry->Next;
        }
        pc->HeapStackTop = pc->StackFrame;
        pc->StackFrame = *(void **)pc->StackFrame;
#ifdef DEBUG_HEAP
        printf("Popping stack frame back to 0x%lx\n", (PTR_TYPE_INT)pc->HeapStackTop);
#endif
        return TRUE;
    }
    else
        return FALSE;
}

/* allocate some dynamically allocated memory. memory is cleared. can return NULL if out of memory */
void *HeapAllocMem(Picoc *pc, int Size)
{
#ifdef USE_MALLOC_HEAP
    return calloc(Size, 1);
#else
    struct AllocNode *NewMem = NULL;
    struct AllocNode **FreeNode;
    int AllocSize = MEM_ALIGN(Size) + MEM_ALIGN(sizeof(NewMem->Size));
    int Bucket;
    void *ReturnMem;

    if (Size == 0)
        return NULL;

    assert(Size > 0);

    /* make sure we have enough space for an AllocNode */
    if (AllocSize < sizeof(struct AllocNode))
        AllocSize = sizeof(struct AllocNode);

    Bucket = AllocSize >> 2;
    if (Bucket < FREELIST_BUCKETS && pc->FreeListBucket[Bucket] != NULL)
    {
        /* try to allocate from a freelist bucket first (exact size-class) */
#ifdef DEBUG_HEAP
        printf("allocating %d(%d) from bucket %d", Size, AllocSize, Bucket);
#endif
        NewMem = pc->FreeListBucket[Bucket];
        assert((PTR_TYPE_INT)NewMem >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)NewMem - &(pc->HeapMemory)[0] < HEAP_SIZE);
        pc->FreeListBucket[Bucket] = *(struct AllocNode **)NewMem;
        assert(pc->FreeListBucket[Bucket] == NULL || ((PTR_TYPE_INT)pc->FreeListBucket[Bucket] >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)pc->FreeListBucket[Bucket] - &(pc->HeapMemory)[0] < HEAP_SIZE));
        NewMem->Size = AllocSize;
    }
    else if (pc->FreeListBig != NULL)
    {
        /* best-fit on the big freelist: choose the smallest block that fits.
         * Reduces internal fragmentation vs classic first-fit under mixed IR sizes. */
        struct AllocNode **BestNode = NULL;
        unsigned int BestSize = (unsigned int)INT_MAX;

        for (FreeNode = &pc->FreeListBig; *FreeNode != NULL; FreeNode = &(*FreeNode)->NextFree)
        {
            unsigned int FSize = (*FreeNode)->Size;
            if (FSize >= (unsigned int)AllocSize && FSize < BestSize)
            {
                BestSize = FSize;
                BestNode = FreeNode;
                if (FSize == (unsigned int)AllocSize)
                    break; /* perfect fit, stop early */
            }
        }

        if (BestNode != NULL)
        {
            FreeNode = BestNode;
            assert((PTR_TYPE_INT)*FreeNode >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)*FreeNode - &(pc->HeapMemory)[0] < HEAP_SIZE);
            assert((*FreeNode)->Size < HEAP_SIZE && (*FreeNode)->Size > 0);
            if ((*FreeNode)->Size < (unsigned int)(AllocSize + SPLIT_MEM_THRESHOLD))
            {
                /* close in size - reduce fragmentation by not splitting */
#ifdef DEBUG_HEAP
               printf("allocating %d(%d) from freelist, no split (%u)", Size, AllocSize, (*FreeNode)->Size);
#endif
                NewMem = *FreeNode;
                assert((PTR_TYPE_INT)NewMem >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)NewMem - &(pc->HeapMemory)[0] < HEAP_SIZE);
                *FreeNode = NewMem->NextFree;
            }
            else
            {
                /* split this big memory chunk */
#ifdef DEBUG_HEAP
                printf("allocating %d(%d) from freelist, split chunk (%u)", Size, AllocSize, (*FreeNode)->Size);
#endif
                NewMem = (void *)((char *)*FreeNode + (*FreeNode)->Size - AllocSize);
                assert((PTR_TYPE_INT)NewMem >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)NewMem - &(pc->HeapMemory)[0] < HEAP_SIZE);
                (*FreeNode)->Size -= AllocSize;
                NewMem->Size = AllocSize;
            }
        }
    }

    if (NewMem == NULL)
    {
        /* couldn't allocate from a freelist - try to increase the size of the heap area */
#ifdef DEBUG_HEAP
        printf("allocating %d(%d) at bottom of heap (0x%lx-0x%lx)", Size, AllocSize, (long)((char *)pc->HeapBottom - AllocSize), (long)HeapBottom);
#endif
        if ((char *)pc->HeapBottom - AllocSize < (char *)pc->HeapStackTop)
            return NULL;

        pc->HeapBottom = (void *)((char *)pc->HeapBottom - AllocSize);
        NewMem = pc->HeapBottom;
        NewMem->Size = AllocSize;
    }

    ReturnMem = (void *)((char *)NewMem + MEM_ALIGN(sizeof(NewMem->Size)));
    memset(ReturnMem, '\0', AllocSize - MEM_ALIGN(sizeof(NewMem->Size)));
#ifdef DEBUG_HEAP
    printf(" = %lx\n", (PTR_TYPE_INT)ReturnMem);
#endif
    return ReturnMem;
#endif
}

/* free some dynamically allocated memory */
void HeapFreeMem(Picoc *pc, void *Mem)
{
    if (Mem == NULL)
        return;
#ifdef USE_MALLOC_HEAP
    free(Mem);
#else
    struct AllocNode *MemNode = (struct AllocNode *)((char *)Mem - MEM_ALIGN(sizeof(MemNode->Size)));
    int Bucket = MemNode->Size >> 2;

#ifdef DEBUG_HEAP
    printf("HeapFreeMem(0x%lx)\n", (PTR_TYPE_INT)Mem);
#endif
    assert((PTR_TYPE_INT)Mem >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)Mem - &(pc->HeapMemory)[0] < HEAP_SIZE);
    assert(MemNode->Size < HEAP_SIZE && MemNode->Size > 0);

    if ((void *)MemNode == pc->HeapBottom)
    {
        /* pop it off the bottom of the heap, reducing the heap size */
#ifdef DEBUG_HEAP
        printf("freeing %d from bottom of heap\n", MemNode->Size);
#endif
        pc->HeapBottom = (void *)((char *)pc->HeapBottom + MemNode->Size);
#ifdef DEBUG_HEAP
        ShowBigList(pc);
#endif
    }
    else if (Bucket < FREELIST_BUCKETS)
    {
        /* exact size-class bucket (now covers up to ~128 B) */
#ifdef DEBUG_HEAP
        printf("freeing %d to bucket %d\n", MemNode->Size, Bucket);
#endif
        assert(pc->FreeListBucket[Bucket] == NULL || ((PTR_TYPE_INT)pc->FreeListBucket[Bucket] >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)pc->FreeListBucket[Bucket] - &(pc->HeapMemory)[0] < HEAP_SIZE));
        *(struct AllocNode **)MemNode = pc->FreeListBucket[Bucket];
        pc->FreeListBucket[Bucket] = (struct AllocNode *)MemNode;
    }
    else
    {
        /* put it in the big memory freelist */
#ifdef DEBUG_HEAP
        printf("freeing %lx:%d to freelist\n", (PTR_TYPE_INT)Mem, MemNode->Size);
#endif
        assert(pc->FreeListBig == NULL || ((PTR_TYPE_INT)pc->FreeListBig >= (PTR_TYPE_INT)&(pc->HeapMemory)[0] && (unsigned char *)pc->FreeListBig - &(pc->HeapMemory)[0] < HEAP_SIZE));
        MemNode->NextFree = pc->FreeListBig;
        pc->FreeListBig = MemNode;
#ifdef DEBUG_HEAP
        ShowBigList(pc);
#endif
    }
#endif
}

