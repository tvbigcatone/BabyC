/*  */
#include <stddef.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "../interpreter.h"
#include "../platform/guest_thread.h"
#include <string.h>


static int Stdlib_ZeroValue = 0;


void StdlibAtof(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "atof: string argument is NULL (undefined behavior in C)");
    ReturnValue->Val->FP = atof(Param[0]->Val->Pointer);
}

void StdlibAtoi(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "atoi: string argument is NULL (undefined behavior in C)");
    ReturnValue->Val->Integer = atoi(Param[0]->Val->Pointer);
}

void StdlibAtol(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "atol: string argument is NULL (undefined behavior in C)");
    ReturnValue->Val->Integer = atol(Param[0]->Val->Pointer);
}

void StdlibStrtod(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "strtod: string argument is NULL (undefined behavior in C)");
    ReturnValue->Val->FP = strtod(Param[0]->Val->Pointer, Param[1]->Val->Pointer);
}

void StdlibStrtol(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    int base = Param[2]->Val->Integer;
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "strtol: string argument is NULL (undefined behavior in C)");
    if (base != 0 && (base < 2 || base > 36))
        ProgramFail(Parser, "strtol: base must be 0 or in the range 2..36 (undefined behavior in C)");
    ReturnValue->Val->Integer = strtol(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, base);
}

void StdlibStrtoul(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    int base = Param[2]->Val->Integer;
    if (Param[0]->Val->Pointer == NULL)
        ProgramFail(Parser, "strtoul: string argument is NULL (undefined behavior in C)");
    if (base != 0 && (base < 2 || base > 36))
        ProgramFail(Parser, "strtoul: base must be 0 or in the range 2..36 (undefined behavior in C)");
    ReturnValue->Val->Integer = strtoul(Param[0]->Val->Pointer,
        Param[1]->Val->Pointer, base);
}

/* ---- Pedagogic heap tracker: live malloc blocks + recently-freed set ----
 * Live table + open-address hash (pointer -> live index) so free/register
 * stay ~O(1). CheckWrite still scans live ranges (needed for past-end). */
#define BABYC_HEAP_TRACK_MAX 16384
#define BABYC_HEAP_FREED_MAX 64
#define BABYC_HEAP_HASH_SIZE 32768

typedef struct {
    void *Ptr;
    size_t Size;
} BabycHeapBlock;

static BabycHeapBlock BabycHeapLive[BABYC_HEAP_TRACK_MAX];
static int BabycHeapLiveN = 0;
static void *BabycHeapFreed[BABYC_HEAP_FREED_MAX];
static int BabycHeapFreedN = 0;
/* Hash slot holds live index+1 (0 = empty). */
static unsigned short BabycHeapHash[BABYC_HEAP_HASH_SIZE];

static unsigned BabycHeapHashKey(void *P)
{
    return (unsigned)(((uintptr_t)P >> 3) & (BABYC_HEAP_HASH_SIZE - 1));
}

static void BabycHeapHashClear(void)
{
    int i;
    for (i = 0; i < BABYC_HEAP_HASH_SIZE; i++)
        BabycHeapHash[i] = 0;
}

static void BabycHeapHashInsert(void *P, int LiveIdx)
{
    unsigned h = BabycHeapHashKey(P);
    int n = 0;
    while (BabycHeapHash[h] != 0 && n < BABYC_HEAP_HASH_SIZE)
    {
        h = (h + 1) & (BABYC_HEAP_HASH_SIZE - 1);
        n++;
    }
    if (n < BABYC_HEAP_HASH_SIZE)
        BabycHeapHash[h] = (unsigned short)(LiveIdx + 1);
}

static void BabycHeapHashDelete(void *P)
{
    unsigned h = BabycHeapHashKey(P);
    int n = 0;
    while (BabycHeapHash[h] != 0 && n < BABYC_HEAP_HASH_SIZE)
    {
        int idx = (int)BabycHeapHash[h] - 1;
        if (idx >= 0 && idx < BabycHeapLiveN && BabycHeapLive[idx].Ptr == P)
        {
            BabycHeapHash[h] = 0;
            /* rehash cluster after hole (simple linear probe repair) */
            {
                unsigned h2 = (h + 1) & (BABYC_HEAP_HASH_SIZE - 1);
                while (BabycHeapHash[h2] != 0)
                {
                    int i2 = (int)BabycHeapHash[h2] - 1;
                    BabycHeapHash[h2] = 0;
                    if (i2 >= 0 && i2 < BabycHeapLiveN && BabycHeapLive[i2].Ptr != NULL)
                        BabycHeapHashInsert(BabycHeapLive[i2].Ptr, i2);
                    h2 = (h2 + 1) & (BABYC_HEAP_HASH_SIZE - 1);
                }
            }
            return;
        }
        h = (h + 1) & (BABYC_HEAP_HASH_SIZE - 1);
        n++;
    }
}

static void BabycHeapForgetFreed(void *P)
{
    int i, j;
    for (i = 0; i < BabycHeapFreedN; i++)
    {
        if (BabycHeapFreed[i] == P)
        {
            for (j = i; j < BabycHeapFreedN - 1; j++)
                BabycHeapFreed[j] = BabycHeapFreed[j + 1];
            BabycHeapFreedN--;
            return;
        }
    }
}

static void BabycHeapMarkFreed(void *P)
{
    int i;
    if (P == NULL)
        return;
    for (i = 0; i < BabycHeapFreedN; i++)
        if (BabycHeapFreed[i] == P)
            return;
    if (BabycHeapFreedN < BABYC_HEAP_FREED_MAX)
        BabycHeapFreed[BabycHeapFreedN++] = P;
    else
    {
        for (i = 1; i < BABYC_HEAP_FREED_MAX; i++)
            BabycHeapFreed[i - 1] = BabycHeapFreed[i];
        BabycHeapFreed[BABYC_HEAP_FREED_MAX - 1] = P;
    }
}

static int BabycHeapFindLive(void *P)
{
    unsigned h;
    int n = 0;
    if (P == NULL || BabycHeapLiveN == 0)
        return -1;
    h = BabycHeapHashKey(P);
    while (BabycHeapHash[h] != 0 && n < BABYC_HEAP_HASH_SIZE)
    {
        int idx = (int)BabycHeapHash[h] - 1;
        if (idx >= 0 && idx < BabycHeapLiveN && BabycHeapLive[idx].Ptr == P)
            return idx;
        h = (h + 1) & (BABYC_HEAP_HASH_SIZE - 1);
        n++;
    }
    return -1;
}

static void BabycHeapRegister(void *P, size_t Sz)
{
    int i;
    if (P == NULL)
        return;
    BabycHeapForgetFreed(P);
    i = BabycHeapFindLive(P);
    if (i >= 0)
    {
        BabycHeapLive[i].Size = Sz;
        return;
    }
    if (BabycHeapLiveN < BABYC_HEAP_TRACK_MAX)
    {
        i = BabycHeapLiveN;
        BabycHeapLive[i].Ptr = P;
        BabycHeapLive[i].Size = Sz;
        BabycHeapLiveN++;
        BabycHeapHashInsert(P, i);
    }
}

void BabycHeapTrackAllocation(void *Ptr, size_t Size)
{
    BabycHeapRegister(Ptr, Size);
}

static void BabycHeapUnregister(void *P)
{
    int i;
    i = BabycHeapFindLive(P);
    if (i < 0)
        return;
    BabycHeapHashDelete(P);
    BabycHeapLiveN--;
    if (i < BabycHeapLiveN)
    {
        void *moved = BabycHeapLive[BabycHeapLiveN].Ptr;
        BabycHeapLive[i] = BabycHeapLive[BabycHeapLiveN];
        /* fix hash for swapped-in entry */
        if (moved != NULL)
        {
            BabycHeapHashDelete(moved);
            BabycHeapHashInsert(moved, i);
        }
    }
    BabycHeapLive[BabycHeapLiveN].Ptr = NULL;
    BabycHeapLive[BabycHeapLiveN].Size = 0;
}

#if defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
static void BabycHeapUpdateAfterRealloc(uintptr_t OldAddr, uintptr_t NewAddr,
                                        int WasTracked)
{
    void *OldKey = (void *)OldAddr;
    if (OldAddr == (uintptr_t)0)
        return;
    if (WasTracked)
        BabycHeapUnregister(OldKey);
    if (NewAddr != OldAddr)
        BabycHeapMarkFreed(OldKey);
    else
        BabycHeapForgetFreed(OldKey);
}

/* True if Addr was returned by malloc/calloc/realloc and later free()'d. */
int BabycHeapIsFreed(const void *Addr)
{
    int i;
    if (Addr == NULL)
        return 0;
    for (i = 0; i < BabycHeapFreedN; i++)
        if (BabycHeapFreed[i] == Addr)
            return 1;
    /* interior of a freed block: only exact base tracked for simplicity */
    return 0;
}

/* If Addr lies in a live malloc block, return 1 and *SizeOut = block size,
 * *BaseOut = block base. Else return 0. */
int BabycHeapLookupLive(const void *Addr, void **BaseOut, size_t *SizeOut)
{
    int i;
    const char *A = (const char *)Addr;
    if (Addr == NULL)
        return 0;
    for (i = 0; i < BabycHeapLiveN; i++)
    {
        char *B = (char *)BabycHeapLive[i].Ptr;
        size_t S = BabycHeapLive[i].Size;
        if (B != NULL && A >= B && A < B + S)
        {
            if (BaseOut) *BaseOut = B;
            if (SizeOut) *SizeOut = S;
            return 1;
        }
    }
    return 0;
}

/* Guard a write of NBytes at Addr. Stack/global/unknown OK; freed or past
 * end of a tracked malloc block → ProgramFail. */
void BabycHeapCheckWrite(struct ParseState *Parser, void *Addr, int NBytes)
{
    int i;
    char *A;
    if (Addr == NULL)
    {
        ProgramFail(Parser, "write to NULL pointer");
        return;
    }
    /* Hot path: no malloc/free yet → nothing to check */
    if (BabycHeapLiveN == 0 && BabycHeapFreedN == 0)
        return;
    if (BabycHeapIsFreed(Addr))
    {
        ProgramFail(Parser, "write to freed memory");
        return;
    }
    if (NBytes < 1)
        NBytes = 1;
    A = (char *)Addr;
    /* Full range scan is O(LiveN). On list-like workloads (thousands of live
     * nodes) this dominates. Cap detailed past-end checks; double-free and
     * write-to-freed remain exact via IsFreed / free path. */
    if (BabycHeapLiveN > 128)
        return;

    /* Pass 1: prefer an allocation that actually contains Addr. Adjacent
     * malloc blocks are legal; checking one-past on an earlier block before
     * looking at the next block falsely rejects writes at the next block's
     * first byte. */
    for (i = 0; i < BabycHeapLiveN; i++)
    {
        char *B = (char *)BabycHeapLive[i].Ptr;
        size_t S = BabycHeapLive[i].Size;
        if (B == NULL || S == 0)
            continue;
        if (A >= B && A < B + (ptrdiff_t)S)
        {
            if ((size_t)(A - B) + (size_t)NBytes > S)
                ProgramFail(Parser, "write past end of allocated block (offset %d, size %d)",
                    (int)(A - B), (int)S);
            return;
        }
    }

    /* Pass 2: only when no allocation contains Addr, detect one-past and
     * short overruns. This preserves the diagnostic without confusing the
     * base of a neighbouring live allocation with the previous block's end. */
    for (i = 0; i < BabycHeapLiveN; i++)
    {
        char *B = (char *)BabycHeapLive[i].Ptr;
        size_t S = BabycHeapLive[i].Size;
        if (B == NULL || S == 0)
            continue;
        if (A >= B + (ptrdiff_t)S
            && A < B + (ptrdiff_t)S + (ptrdiff_t)NBytes)
        {
            ProgramFail(Parser, "write past end of allocated block (offset %d, size %d)",
                (int)(A - B), (int)S);
            return;
        }
    }
}

void StdlibMalloc(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    int n = Param[0]->Val->Integer;
    void *p;
    (void)Parser;
    if (n < 0)
        n = 0;
    p = malloc((size_t)n);
    BabycHeapRegister(p, (size_t)n);
    ReturnValue->Val->Pointer = p;
}

void StdlibCalloc(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    int n = Param[0]->Val->Integer;
    int sz = Param[1]->Val->Integer;
    void *p;
    size_t total;
    (void)Parser;
    if (n < 0) n = 0;
    if (sz < 0) sz = 0;
    if (n > 0 && sz > 0 && (size_t)n > (size_t)-1 / (size_t)sz)
    {
        /* The product cannot be represented as size_t. Do not register a
         * wrapped size or expose a smaller-than-requested allocation. */
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    total = (size_t)n * (size_t)sz;
    p = calloc((size_t)n, (size_t)sz);
    BabycHeapRegister(p, total);
    ReturnValue->Val->Pointer = p;
}

void StdlibRealloc(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    void *oldp = Param[0]->Val->Pointer;
    uintptr_t oldaddr = (uintptr_t)oldp;
    int n = Param[1]->Val->Integer;
    int old_tracked = 0;
    void *p;
    (void)Parser;
    if (n < 0)
        n = 0;
    if (oldp != NULL)
    {
        if (BabycHeapIsFreed(oldp))
            ProgramFail(Parser, "realloc of freed memory");
        old_tracked = BabycHeapFindLive(oldp) >= 0;
        if (!old_tracked)
            ProgramFail(Parser, "realloc of pointer not allocated by malloc/calloc/realloc/strdup (undefined behavior in C)");
    }
    p = realloc(oldp, (size_t)n);
    /* A failed nonzero realloc leaves the old allocation alive and tracked. */
    if (p == NULL && n > 0)
    {
        ReturnValue->Val->Pointer = NULL;
        return;
    }
    BabycHeapUpdateAfterRealloc(oldaddr, (uintptr_t)p, old_tracked);
    BabycHeapRegister(p, (size_t)n);
    ReturnValue->Val->Pointer = p;
}

void StdlibFree(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    void *p = Param[0]->Val->Pointer;
    (void)ReturnValue;
    if (p == NULL)
        return; /* free(NULL) is defined no-op */
    if (BabycHeapIsFreed(p))
        ProgramFail(Parser, "double free");
    if (BabycHeapFindLive(p) < 0)
        ProgramFail(Parser, "free of pointer not allocated by malloc/calloc/realloc/strdup (undefined behavior in C)");
    BabycHeapUnregister(p);
    BabycHeapMarkFreed(p);
    free(p);
}

void StdlibRand(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = rand();
}

void StdlibSrand(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    srand(Param[0]->Val->Integer);
}

void StdlibAbort(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ProgramFail(Parser, "abort");
}

void StdlibExit(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    PlatformExit(Parser->pc, Param[0]->Val->Integer);
}

void StdlibGetenv(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Pointer = getenv(Param[0]->Val->Pointer);
}

void StdlibSystem(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = system(Param[0]->Val->Pointer);
}

/* Guest compar trampoline: Param holds Func Value* (see ExpressionAssignToPointer).
 * Never pass guest pointers to host libc qsort/bsearch — that was the root bug. */
static int StdlibCompar(struct ParseState *Parser, struct Value *CmpParam,
    const void *a, const void *b)
{
    struct Value *fv;
    (void)Parser;
    fv = BabycGuestResolveFuncValue(CmpParam);
    if (fv == NULL)
        ProgramFail(Parser, "qsort/bsearch: comparator is not a valid function pointer");
    return BabycGuestCallInt2(fv, (void *)a, (void *)b);
}

/* Stable bottom-up merge sort: deterministic for elements that compare equal
 * (heapsort is not stable), O(n log n), no recursion. */
void StdlibQsort(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    char *base, *src, *dst, *tmp, *swp;
    long long nmemb, size;
    size_t n, sz, width, i;
    struct Value *Cmp;
    (void)ReturnValue;
    (void)NumArgs;
    if (Param == NULL || Param[0] == NULL)
        return;
    base = (char *)Param[0]->Val->Pointer;
    nmemb = Param[1]->Val->Integer;
    size = Param[2]->Val->Integer;
    Cmp = Param[3];
    if (nmemb < 0)
        ProgramFail(Parser, "qsort: negative element count is invalid");
    if (nmemb == 0)
        return;
    if (base == NULL)
        ProgramFail(Parser, "qsort: base is NULL for a non-empty array (undefined behavior in C)");
    if (size <= 0)
        ProgramFail(Parser, "qsort: element size must be positive");
    if (Cmp == NULL || BabycGuestResolveFuncValue(Cmp) == NULL)
        ProgramFail(Parser, "qsort: comparator is NULL or invalid (undefined behavior in C)");
    if (nmemb == 1)
        return;
    n = (size_t)nmemb;
    sz = (size_t)size;
    if (n > (size_t)-1 / sz)
        ProgramFail(Parser, "qsort: array too large");
    tmp = (char *)malloc(n * sz);
    if (tmp == NULL)
        ProgramFail(Parser, DiagnosticMsgOutOfMemory);
    BabycGuestSetPC(Parser->pc);
    src = base;
    dst = tmp;
    for (width = 1; width < n; width *= 2) {
        for (i = 0; i < n; i += 2 * width) {
            size_t mid = (i + width < n) ? i + width : n;
            size_t hi = (i + 2 * width < n) ? i + 2 * width : n;
            size_t l = i, r = mid, k = i;
            while (l < mid && r < hi) {
                if (StdlibCompar(Parser, Cmp, src + l * sz, src + r * sz) <= 0)
                    memcpy(dst + (k++) * sz, src + (l++) * sz, sz);
                else
                    memcpy(dst + (k++) * sz, src + (r++) * sz, sz);
            }
            if (l < mid)
                memcpy(dst + k * sz, src + l * sz, (mid - l) * sz);
            else if (r < hi)
                memcpy(dst + k * sz, src + r * sz, (hi - r) * sz);
        }
        swp = src; src = dst; dst = swp;
    }
    if (src != base)
        memcpy(base, src, n * sz);
    free(tmp);
}

void StdlibBsearch(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    const char *key;
    const char *base;
    int nmemb, size;
    struct Value *Cmp;
    int lo, hi;
    (void)NumArgs;
    ReturnValue->Val->Pointer = NULL;
    if (Param == NULL)
        return;
    key = (const char *)Param[0]->Val->Pointer;
    base = (const char *)Param[1]->Val->Pointer;
    nmemb = Param[2]->Val->Integer;
    size = Param[3]->Val->Integer;
    Cmp = Param[4];
    if (nmemb < 0)
        ProgramFail(Parser, "bsearch: negative element count is invalid");
    if (nmemb == 0)
        return;
    if (key == NULL || base == NULL)
        ProgramFail(Parser, "bsearch: key or base is NULL for a non-empty array (undefined behavior in C)");
    if (size <= 0)
        ProgramFail(Parser, "bsearch: element size must be positive");
    if ((size_t)nmemb > (size_t)-1 / (size_t)size)
        ProgramFail(Parser, "bsearch: array size overflows size_t");
    if (Cmp == NULL || BabycGuestResolveFuncValue(Cmp) == NULL)
        ProgramFail(Parser, "bsearch: comparator is NULL or invalid (undefined behavior in C)");
    BabycGuestSetPC(Parser->pc);
    lo = 0;
    hi = nmemb - 1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        const char *elem = base + (size_t)mid * (size_t)size;
        int c = StdlibCompar(Parser, Cmp, key, elem);
        if (c < 0)
            hi = mid - 1;
        else if (c > 0)
            lo = mid + 1;
        else {
            ReturnValue->Val->Pointer = (void *)elem;
            return;
        }
    }
}

void StdlibAbs(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    if (Param[0]->Val->Integer == INT_MIN)
        ProgramFail(Parser, "abs(INT_MIN) has undefined behavior in C");
    ReturnValue->Val->Integer = abs((int)Param[0]->Val->Integer);
}

void StdlibLabs(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    if ((long)Param[0]->Val->Integer == LONG_MIN)
        ProgramFail(Parser, "labs(LONG_MIN) has undefined behavior in C");
    ReturnValue->Val->Integer = labs((long)Param[0]->Val->Integer);
}

#if 0
void StdlibDiv(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = div(Param[0]->Val->Integer,
        Param[1]->Val->Integer);
}

void StdlibLdiv(struct ParseState *Parser, struct Value *ReturnValue,
    struct Value **Param, int NumArgs)
{
    ReturnValue->Val->Integer = ldiv(Param[0]->Val->Integer,
        Param[1]->Val->Integer);
}
#endif

#if 0
/* handy structure definitions */
const char StdlibDefs[] = "\
typedef struct { \
    int quot, rem; \
} div_t; \
\
typedef struct { \
    int quot, rem; \
} ldiv_t; \
";
#endif

/* all stdlib.h functions */
struct LibraryFunction StdlibFunctions[] =
{
    {StdlibAtof, "float atof(char *);"},
    {StdlibStrtod, "float strtod(char *,char **);"},
    {StdlibAtoi, "int atoi(char *);"},
    {StdlibAtol, "int atol(char *);"},
    {StdlibStrtol, "int strtol(char *,char **,int);"},
    {StdlibStrtoul, "int strtoul(char *,char **,int);"},
    {StdlibMalloc, "void *malloc(int);"},
    {StdlibCalloc, "void *calloc(int,int);"},
    {StdlibRealloc, "void *realloc(void *,int);"},
    {StdlibFree, "void free(void *);"},
    {StdlibRand, "int rand();"},
    {StdlibSrand, "void srand(int);"},
    {StdlibAbort, "void abort();"},
    {StdlibExit, "void exit(int);"},
    {StdlibGetenv, "char *getenv(char *);"},
    {StdlibSystem, "int system(char *);"},
{StdlibBsearch, "void *bsearch(void *, void *, int, int, int (*)(void *, void *));"},
    {StdlibQsort, "void qsort(void *, int, int, int (*)(void *, void *));"},
    {StdlibAbs, "int abs(int);"},
    {StdlibLabs, "int labs(int);"},
#if 0
    {StdlibDiv, "div_t div(int);"},
    {StdlibLdiv, "ldiv_t ldiv(int);"},
#endif
    {NULL, NULL}
};

/* creates various system-dependent definitions */
void StdlibSetupFunc(Picoc *pc)
{
    /* define NULL, TRUE and FALSE */
    if (!VariableDefined(pc, TableStrRegister(pc, "NULL")))
        VariableDefinePlatformVar(pc, NULL, "NULL", &pc->IntType,
            (union AnyValue*)&Stdlib_ZeroValue, FALSE);
}

