/* BabyPOSIX leftovers for coreutils: select/poll, rlimit, strings, locale,
 * inttypes, stddef. Guest layouts stay independent of the host. */
#include "posix_more.h"
#include "posix_fd.h"
#include "../platform/guest_thread.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#ifdef WIN32
# include <windows.h>
#else
# include <sys/select.h>
# include <sys/resource.h>
# include <sys/file.h>
# include <poll.h>
# include <locale.h>
# include <strings.h>
# include <fnmatch.h>
# include <sched.h>
#endif

#define BP_FD_SETSIZE 256
#define BP_FD_NFWORDS 8

struct BabyPosixFdSet {
    unsigned int bits[BP_FD_NFWORDS];
};

static int BabyPosixFdSetBit(const struct BabyPosixFdSet *s, int fd)
{
    unsigned int w, b;
    if (fd < 0 || fd >= BP_FD_SETSIZE || s == NULL) return 0;
    w = (unsigned int)fd / 32u;
    b = (unsigned int)fd % 32u;
    return (s->bits[w] >> b) & 1u;
}

static void BabyPosixFdSetPut(struct BabyPosixFdSet *s, int fd, int on)
{
    unsigned int w, b, m;
    if (fd < 0 || fd >= BP_FD_SETSIZE || s == NULL) return;
    w = (unsigned int)fd / 32u;
    b = (unsigned int)fd % 32u;
    m = 1u << b;
    if (on) s->bits[w] |= m;
    else s->bits[w] &= ~m;
}

static void BabyPosixSelectCall(struct ParseState *p, struct Value *r,
                                struct Value **a, int n)
{
    int nfds = (int)a[0]->Val->Integer;
    struct BabyPosixFdSet *gr = n > 1 ? (struct BabyPosixFdSet *)a[1]->Val->Pointer : NULL;
    struct BabyPosixFdSet *gw = n > 2 ? (struct BabyPosixFdSet *)a[2]->Val->Pointer : NULL;
    struct BabyPosixFdSet *gx = n > 3 ? (struct BabyPosixFdSet *)a[3]->Val->Pointer : NULL;
    struct BabyPosixTimeval { long long tv_sec; long long tv_usec; };
    struct BabyPosixTimeval *gtv = n > 4 ? (struct BabyPosixTimeval *)a[4]->Val->Pointer : NULL;
#ifdef WIN32
    (void)p; (void)nfds; (void)gr; (void)gw; (void)gx; (void)gtv; (void)n;
    errno = ENOSYS; r->Val->Integer = -1; return;
#else
    int gil, i, native, maxn, rc;
    int map[BP_FD_SETSIZE];
    fd_set nr, nw, nx;
    struct timeval tv, *tvp;
    (void)p;
    if (nfds < 0) { errno = EINVAL; r->Val->Integer = -1; return; }
    if (nfds > BP_FD_SETSIZE) nfds = BP_FD_SETSIZE;
    FD_ZERO(&nr); FD_ZERO(&nw); FD_ZERO(&nx);
    maxn = -1;
    for (i = 0; i < BP_FD_SETSIZE; i++) map[i] = -1;
    for (i = 0; i < nfds; i++) {
        int want = 0;
        if (gr && BabyPosixFdSetBit(gr, i)) want = 1;
        if (gw && BabyPosixFdSetBit(gw, i)) want = 1;
        if (gx && BabyPosixFdSetBit(gx, i)) want = 1;
        if (!want) continue;
        native = BabyPosixFdNativeOf(i);
        if (native < 0) { r->Val->Integer = -1; return; }
        if (native >= FD_SETSIZE) { errno = EBADF; r->Val->Integer = -1; return; }
        map[native] = i;
        if (gr && BabyPosixFdSetBit(gr, i)) FD_SET(native, &nr);
        if (gw && BabyPosixFdSetBit(gw, i)) FD_SET(native, &nw);
        if (gx && BabyPosixFdSetBit(gx, i)) FD_SET(native, &nx);
        if (native > maxn) maxn = native;
    }
    if (gtv) {
        tv.tv_sec = (time_t)gtv->tv_sec;
        tv.tv_usec = (suseconds_t)gtv->tv_usec;
        tvp = &tv;
    } else tvp = NULL;
    gil = BabycGuestGIL_UnlockIfHeld();
    rc = select(maxn + 1, gr ? &nr : NULL, gw ? &nw : NULL, gx ? &nx : NULL, tvp);
    BabycGuestGIL_RelockIf(gil);
    if (rc < 0) { r->Val->Integer = -1; return; }
    if (gr) memset(gr, 0, sizeof(*gr));
    if (gw) memset(gw, 0, sizeof(*gw));
    if (gx) memset(gx, 0, sizeof(*gx));
    for (native = 0; native <= maxn; native++) {
        int g = map[native];
        if (g < 0) continue;
        if (gr && FD_ISSET(native, &nr)) BabyPosixFdSetPut(gr, g, 1);
        if (gw && FD_ISSET(native, &nw)) BabyPosixFdSetPut(gw, g, 1);
        if (gx && FD_ISSET(native, &nx)) BabyPosixFdSetPut(gx, g, 1);
    }
    r->Val->Integer = rc;
#endif
}

static void BabyPosixFdZeroCall(struct ParseState *p, struct Value *r,
                                struct Value **a, int n)
{
    struct BabyPosixFdSet *s = (struct BabyPosixFdSet *)a[0]->Val->Pointer;
    (void)p; (void)n; (void)r;
    if (s == NULL) return;
    memset(s, 0, sizeof(*s));
}

static void BabyPosixFdSetCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    BabyPosixFdSetPut((struct BabyPosixFdSet *)a[1]->Val->Pointer,
        (int)a[0]->Val->Integer, 1);
}

static void BabyPosixFdClrCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    BabyPosixFdSetPut((struct BabyPosixFdSet *)a[1]->Val->Pointer,
        (int)a[0]->Val->Integer, 0);
}

static void BabyPosixFdIssetCall(struct ParseState *p, struct Value *r,
                                 struct Value **a, int n)
{
    (void)p; (void)n;
    r->Val->Integer = BabyPosixFdSetBit(
        (struct BabyPosixFdSet *)a[1]->Val->Pointer, (int)a[0]->Val->Integer);
}

const char BabyPosixSelectDefs[] =
    "#define FD_SETSIZE 256\n"
    "typedef struct { unsigned int bits[8]; } fd_set;\n"
    "#include <sys/time.h>\n";

struct LibraryFunction BabyPosixSelectFunctions[] = {
    { BabyPosixSelectCall, "int select(int, fd_set *, fd_set *, fd_set *, struct timeval *);" },
    { BabyPosixFdZeroCall, "void FD_ZERO(fd_set *);" },
    { BabyPosixFdSetCall, "void FD_SET(int, fd_set *);" },
    { BabyPosixFdClrCall, "void FD_CLR(int, fd_set *);" },
    { BabyPosixFdIssetCall, "int FD_ISSET(int, fd_set *);" },
    { NULL, NULL }
};

void BabyPosixSelectSetupFunc(Picoc *pc) { (void)pc; }

struct BabyPosixPollfd {
    int fd;
    short events;
    short revents;
};

#define BP_POLLIN 0x001
#define BP_POLLOUT 0x004
#define BP_POLLERR 0x008
#define BP_POLLHUP 0x010
#define BP_POLLNVAL 0x020

static void BabyPosixPollCall(struct ParseState *p, struct Value *r,
                              struct Value **a, int n)
{
    struct BabyPosixPollfd *fds = (struct BabyPosixPollfd *)a[0]->Val->Pointer;
    int nfds = (int)a[1]->Val->Integer;
    int timeout = (int)a[2]->Val->Integer;
    int gil, i, rc;
    (void)p; (void)n;
    if (nfds < 0) { errno = EINVAL; r->Val->Integer = -1; return; }
    if (nfds == 0) { r->Val->Integer = 0; return; }
    if (fds == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
#ifdef WIN32
    (void)timeout; (void)gil; (void)i; (void)rc;
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct pollfd *host;
        unsigned char *nval;
        int extra = 0, saved_errno;
        host = (struct pollfd *)malloc((size_t)nfds * sizeof(*host));
        nval = (unsigned char *)calloc((size_t)nfds, 1);
        if (host == NULL || nval == NULL) {
            free(host); free(nval);
            errno = ENOMEM; r->Val->Integer = -1; return;
        }
        saved_errno = errno;
        for (i = 0; i < nfds; i++) {
            host[i].events = (short)fds[i].events;
            host[i].revents = 0;
            if (fds[i].fd < 0) {
                host[i].fd = -1;
            } else {
                int native = BabyPosixFdNativeOf(fds[i].fd);
                if (native < 0) {
                    host[i].fd = -1;
                    nval[i] = 1;
                    extra++;
                } else
                    host[i].fd = native;
            }
        }
        errno = saved_errno;
        /* POLLNVAL is a ready event: do not block on remaining fds. */
        if (extra > 0 && timeout != 0)
            timeout = 0;
        gil = BabycGuestGIL_UnlockIfHeld();
        rc = poll(host, (nfds_t)nfds, timeout);
        BabycGuestGIL_RelockIf(gil);
        if (rc >= 0) {
            for (i = 0; i < nfds; i++)
                fds[i].revents = nval[i] ? (short)BP_POLLNVAL : host[i].revents;
            rc += extra;
        }
        free(host);
        free(nval);
        r->Val->Integer = rc;
    }
#endif
}

const char BabyPosixPollDefs[] =
    "struct pollfd { int fd; short events; short revents; };\n"
    "typedef unsigned int nfds_t;\n"
    "#define POLLIN 1\n"
    "#define POLLPRI 2\n"
    "#define POLLOUT 4\n"
    "#define POLLERR 8\n"
    "#define POLLHUP 16\n"
    "#define POLLNVAL 32\n";

struct LibraryFunction BabyPosixPollFunctions[] = {
    { BabyPosixPollCall, "int poll(struct pollfd *, unsigned int, int);" },
    { NULL, NULL }
};

void BabyPosixPollSetupFunc(Picoc *pc) { (void)pc; }

struct BabyPosixRlimit {
    long rlim_cur;
    long rlim_max;
};

static void BabyPosixGetrlimitCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    struct BabyPosixRlimit *out = (struct BabyPosixRlimit *)a[1]->Val->Pointer;
    (void)p; (void)n;
    if (out == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct rlimit rl;
        if (getrlimit((int)a[0]->Val->Integer, &rl) != 0) {
            r->Val->Integer = -1; return;
        }
        out->rlim_cur = (long)rl.rlim_cur;
        out->rlim_max = (long)rl.rlim_max;
        r->Val->Integer = 0;
    }
#endif
}

static void BabyPosixSetrlimitCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    struct BabyPosixRlimit *in = (struct BabyPosixRlimit *)a[1]->Val->Pointer;
    (void)p; (void)n;
    if (in == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct rlimit rl;
        rl.rlim_cur = (rlim_t)in->rlim_cur;
        rl.rlim_max = (rlim_t)in->rlim_max;
        r->Val->Integer = setrlimit((int)a[0]->Val->Integer, &rl);
    }
#endif
}

const char BabyPosixResourceDefs[] =
    "struct rlimit { long rlim_cur; long rlim_max; };\n"
    "#define RLIMIT_CPU 0\n"
    "#define RLIMIT_FSIZE 1\n"
    "#define RLIMIT_DATA 2\n"
    "#define RLIMIT_STACK 3\n"
    "#define RLIMIT_CORE 4\n"
    "#define RLIMIT_NOFILE 7\n"
    "#define RLIMIT_AS 9\n"
    "#define RLIM_INFINITY (-1)\n"
    "#include <sys/time.h>\n"
    "#define RUSAGE_SELF 0\n"
    "#define RUSAGE_CHILDREN (-1)\n"
    "struct rusage {\n"
    "    struct timeval ru_utime;\n"
    "    struct timeval ru_stime;\n"
    "    long ru_maxrss;\n"
    "    long ru_ixrss;\n"
    "    long ru_idrss;\n"
    "    long ru_isrss;\n"
    "    long ru_minflt;\n"
    "    long ru_majflt;\n"
    "    long ru_nswap;\n"
    "    long ru_inblock;\n"
    "    long ru_oublock;\n"
    "    long ru_msgsnd;\n"
    "    long ru_msgrcv;\n"
    "    long ru_nsignals;\n"
    "    long ru_nvcsw;\n"
    "    long ru_nivcsw;\n"
    "};\n";

struct BabyPosixRusage {
    struct { long long tv_sec; long long tv_usec; } ru_utime;
    struct { long long tv_sec; long long tv_usec; } ru_stime;
    long ru_maxrss;
    long ru_ixrss;
    long ru_idrss;
    long ru_isrss;
    long ru_minflt;
    long ru_majflt;
    long ru_nswap;
    long ru_inblock;
    long ru_oublock;
    long ru_msgsnd;
    long ru_msgrcv;
    long ru_nsignals;
    long ru_nvcsw;
    long ru_nivcsw;
};

static void BabyPosixGetrusageCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    struct BabyPosixRusage *out = (struct BabyPosixRusage *)a[1]->Val->Pointer;
    (void)p; (void)n;
    if (out == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct rusage ru;
        if (getrusage((int)a[0]->Val->Integer, &ru) != 0) {
            r->Val->Integer = -1; return;
        }
        memset(out, 0, sizeof(*out));
        out->ru_utime.tv_sec = (long long)ru.ru_utime.tv_sec;
        out->ru_utime.tv_usec = (long long)ru.ru_utime.tv_usec;
        out->ru_stime.tv_sec = (long long)ru.ru_stime.tv_sec;
        out->ru_stime.tv_usec = (long long)ru.ru_stime.tv_usec;
        out->ru_maxrss = (long)ru.ru_maxrss;
        out->ru_ixrss = (long)ru.ru_ixrss;
        out->ru_idrss = (long)ru.ru_idrss;
        out->ru_isrss = (long)ru.ru_isrss;
        out->ru_minflt = (long)ru.ru_minflt;
        out->ru_majflt = (long)ru.ru_majflt;
        out->ru_nswap = (long)ru.ru_nswap;
        out->ru_inblock = (long)ru.ru_inblock;
        out->ru_oublock = (long)ru.ru_oublock;
        out->ru_msgsnd = (long)ru.ru_msgsnd;
        out->ru_msgrcv = (long)ru.ru_msgrcv;
        out->ru_nsignals = (long)ru.ru_nsignals;
        out->ru_nvcsw = (long)ru.ru_nvcsw;
        out->ru_nivcsw = (long)ru.ru_nivcsw;
        r->Val->Integer = 0;
    }
#endif
}

struct LibraryFunction BabyPosixResourceFunctions[] = {
    { BabyPosixGetrlimitCall, "int getrlimit(int, struct rlimit *);" },
    { BabyPosixSetrlimitCall, "int setrlimit(int, struct rlimit *);" },
    { BabyPosixGetrusageCall, "int getrusage(int, struct rusage *);" },
    { NULL, NULL }
};

void BabyPosixResourceSetupFunc(Picoc *pc) { (void)pc; }

static void BabyPosixBzeroCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    if (a[0]->Val->Pointer == NULL) return;
    memset(a[0]->Val->Pointer, 0, (size_t)a[1]->Val->Integer);
}

static void BabyPosixBcopyCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    if (a[0]->Val->Pointer == NULL || a[1]->Val->Pointer == NULL) return;
    memmove(a[1]->Val->Pointer, a[0]->Val->Pointer, (size_t)a[2]->Val->Integer);
}

static void BabyPosixBcmpCall(struct ParseState *p, struct Value *r,
                              struct Value **a, int n)
{
    (void)p; (void)n;
    if (a[0]->Val->Pointer == NULL || a[1]->Val->Pointer == NULL) {
        r->Val->Integer = 0; return;
    }
    r->Val->Integer = memcmp(a[0]->Val->Pointer, a[1]->Val->Pointer,
        (size_t)a[2]->Val->Integer);
}

static void BabyPosixFfsCall(struct ParseState *p, struct Value *r,
                             struct Value **a, int n)
{
    int v = (int)a[0]->Val->Integer;
    int i;
    (void)p; (void)n;
    if (v == 0) { r->Val->Integer = 0; return; }
    for (i = 0; i < 32; i++) {
        if ((unsigned int)v & (1u << i)) { r->Val->Integer = i + 1; return; }
    }
    r->Val->Integer = 0;
}

static void BabyPosixIndexCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    const char *s = (const char *)a[0]->Val->Pointer;
    int c = (int)a[1]->Val->Integer;
    (void)p; (void)n;
    if (s == NULL) { r->Val->Pointer = NULL; return; }
    r->Val->Pointer = (void *)strchr(s, c);
}

static void BabyPosixRindexCall(struct ParseState *p, struct Value *r,
                                struct Value **a, int n)
{
    const char *s = (const char *)a[0]->Val->Pointer;
    int c = (int)a[1]->Val->Integer;
    (void)p; (void)n;
    if (s == NULL) { r->Val->Pointer = NULL; return; }
    r->Val->Pointer = (void *)strrchr(s, c);
}

const char BabyPosixStringsDefs[] = "";

struct LibraryFunction BabyPosixStringsFunctions[] = {
    { BabyPosixBzeroCall, "void bzero(void *, size_t);" },
    { BabyPosixBcopyCall, "void bcopy(void *, void *, size_t);" },
    { BabyPosixBcmpCall, "int bcmp(void *, void *, size_t);" },
    { BabyPosixFfsCall, "int ffs(int);" },
    { BabyPosixIndexCall, "char *index(char *, int);" },
    { BabyPosixRindexCall, "char *rindex(char *, int);" },
    { NULL, NULL }
};

void BabyPosixStringsSetupFunc(Picoc *pc) { (void)pc; }

static void BabyPosixSetlocaleCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    (void)p; (void)n;
#ifdef WIN32
    r->Val->Pointer = (void *)"C";
#else
    r->Val->Pointer = setlocale((int)a[0]->Val->Integer,
        (const char *)a[1]->Val->Pointer);
#endif
}

const char BabyPosixLocaleDefs[] =
    "#define LC_CTYPE 0\n"
    "#define LC_NUMERIC 1\n"
    "#define LC_TIME 2\n"
    "#define LC_COLLATE 3\n"
    "#define LC_MONETARY 4\n"
    "#define LC_MESSAGES 5\n"
    "#define LC_ALL 6\n";

struct LibraryFunction BabyPosixLocaleFunctions[] = {
    { BabyPosixSetlocaleCall, "char *setlocale(int, char *);" },
    { NULL, NULL }
};

void BabyPosixLocaleSetupFunc(Picoc *pc) { (void)pc; }

const char BabyPosixInttypesDefs[] =
    "#include <stdint.h>\n"
    "#define PRId8 \"d\"\n"
    "#define PRIi8 \"i\"\n"
    "#define PRIu8 \"u\"\n"
    "#define PRIx8 \"x\"\n"
    "#define PRId16 \"d\"\n"
    "#define PRIu16 \"u\"\n"
    "#define PRIx16 \"x\"\n"
    "#define PRId32 \"d\"\n"
    "#define PRIu32 \"u\"\n"
    "#define PRIx32 \"x\"\n"
    "#define PRId64 \"lld\"\n"
    "#define PRIu64 \"llu\"\n"
    "#define PRIx64 \"llx\"\n"
    "#define PRIdPTR \"ld\"\n"
    "#define PRIuPTR \"lu\"\n";

struct LibraryFunction BabyPosixInttypesFunctions[] = {
    { NULL, NULL }
};

void BabyPosixInttypesSetupFunc(Picoc *pc) { (void)pc; }

const char BabyPosixStddefDefs[] =
    "#ifndef NULL\n"
    "#define NULL 0\n"
    "#endif\n"
    "typedef int ptrdiff_t;\n";

struct LibraryFunction BabyPosixStddefFunctions[] = {
    { NULL, NULL }
};

void BabyPosixStddefSetupFunc(Picoc *pc) { (void)pc; }

const char BabyPosixParamDefs[] =
    "#ifndef MAXPATHLEN\n"
    "#define MAXPATHLEN 4096\n"
    "#endif\n"
    "#ifndef MAXHOSTNAMELEN\n"
    "#define MAXHOSTNAMELEN 256\n"
    "#endif\n"
    "#ifndef MIN\n"
    "#define MIN(a,b) ((a)<(b)?(a):(b))\n"
    "#endif\n"
    "#ifndef MAX\n"
    "#define MAX(a,b) ((a)>(b)?(a):(b))\n"
    "#endif\n";

struct LibraryFunction BabyPosixParamFunctions[] = {
    { NULL, NULL }
};

void BabyPosixParamSetupFunc(Picoc *pc) { (void)pc; }

const char BabyPosixSysmacrosDefs[] =
    "#define major(dev) ((int)(((unsigned int)(dev) >> 8) & 0xff))\n"
    "#define minor(dev) ((int)((unsigned int)(dev) & 0xff))\n"
    "#define makedev(ma,mi) ((unsigned long)((((unsigned int)(ma) & 0xff) << 8) | ((unsigned int)(mi) & 0xff)))\n";

struct LibraryFunction BabyPosixSysmacrosFunctions[] = {
    { NULL, NULL }
};

void BabyPosixSysmacrosSetupFunc(Picoc *pc) { (void)pc; }

static void BabyPosixFlockCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    int fd = (int)a[0]->Val->Integer;
    int op = (int)a[1]->Val->Integer;
    int native;
    (void)p; (void)n;
#ifdef WIN32
    (void)fd; (void)op; (void)native;
    errno = ENOSYS; r->Val->Integer = -1;
#else
    native = BabyPosixFdNativeOf(fd);
    if (native < 0) { r->Val->Integer = -1; return; }
    r->Val->Integer = flock(native, op);
#endif
}

const char BabyPosixFileDefs[] =
    "#define LOCK_SH 1\n"
    "#define LOCK_EX 2\n"
    "#define LOCK_NB 4\n"
    "#define LOCK_UN 8\n";

struct LibraryFunction BabyPosixFileFunctions[] = {
    { BabyPosixFlockCall, "int flock(int, int);" },
    { NULL, NULL }
};

void BabyPosixFileSetupFunc(Picoc *pc) { (void)pc; }

static void BabyPosixFnmatchCall(struct ParseState *p, struct Value *r,
                                 struct Value **a, int n)
{
    const char *pattern = (const char *)a[0]->Val->Pointer;
    const char *string = (const char *)a[1]->Val->Pointer;
    int flags = (int)a[2]->Val->Integer;
    (void)p; (void)n;
    if (pattern == NULL || string == NULL) {
        errno = EFAULT; r->Val->Integer = -1; return;
    }
#ifdef WIN32
    (void)flags;
    errno = ENOSYS; r->Val->Integer = -1;
#else
    r->Val->Integer = fnmatch(pattern, string, flags);
#endif
}

const char BabyPosixFnmatchDefs[] =
    "#define FNM_NOMATCH 1\n"
    "#define FNM_PATHNAME 1\n"
    "#define FNM_NOESCAPE 2\n"
    "#define FNM_PERIOD 4\n"
    "#define FNM_CASEFOLD 16\n";

struct LibraryFunction BabyPosixFnmatchFunctions[] = {
    { BabyPosixFnmatchCall, "int fnmatch(char *, char *, int);" },
    { NULL, NULL }
};

void BabyPosixFnmatchSetupFunc(Picoc *pc) { (void)pc; }

static void BabyPosixSchedYieldCall(struct ParseState *p, struct Value *r,
                                    struct Value **a, int n)
{
    (void)p; (void)a; (void)n;
#ifdef WIN32
    Sleep(0);
    r->Val->Integer = 0;
#else
    r->Val->Integer = sched_yield();
#endif
}

const char BabyPosixSchedDefs[] =
    "#define SCHED_OTHER 0\n"
    "#define SCHED_FIFO 1\n"
    "#define SCHED_RR 2\n";

struct LibraryFunction BabyPosixSchedFunctions[] = {
    { BabyPosixSchedYieldCall, "int sched_yield(void);" },
    { NULL, NULL }
};

void BabyPosixSchedSetupFunc(Picoc *pc) { (void)pc; }
