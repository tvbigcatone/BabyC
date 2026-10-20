/* BabyPOSIX: small, documented POSIX time subset.
 * Keep the interpreted ABI independent from the host's struct timeval layout.
 */
#include "posix_time.h"
#include "../platform/guest_thread.h"

#include <errno.h>
#include <stdint.h>
#ifdef WIN32
# include <windows.h>
#else
# include <sys/time.h>
# include <sys/times.h>
# include <time.h>
#endif
#include <string.h>

struct BabyPosixTimeval {
    long long tv_sec;
    long long tv_usec;
};

static int BabyPosixGettimeofday(struct BabyPosixTimeval *out)
{
    if (out == NULL) {
        errno = EFAULT;
        return -1;
    }

#ifdef WIN32
    {
        FILETIME ft;
        ULARGE_INTEGER ticks;
        const unsigned long long UnixEpochTicks = 116444736000000000ULL;
        unsigned long long unixTicks;

        GetSystemTimeAsFileTime(&ft);
        ticks.LowPart = ft.dwLowDateTime;
        ticks.HighPart = ft.dwHighDateTime;
        if (ticks.QuadPart < UnixEpochTicks)
            return -1;
        unixTicks = ticks.QuadPart - UnixEpochTicks;
        out->tv_sec = (long long)(unixTicks / 10000000ULL);
        out->tv_usec = (long long)((unixTicks % 10000000ULL) / 10ULL);
        return 0;
    }
#else
    {
        struct timeval nativeTime;
        if (gettimeofday(&nativeTime, NULL) != 0)
            return -1;
        out->tv_sec = (long long)nativeTime.tv_sec;
        out->tv_usec = (long long)nativeTime.tv_usec;
        return 0;
    }
#endif
}

static void BabyPosixGettimeofdayCall(struct ParseState *Parser,
    struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    (void)Parser;
    (void)NumArgs;
    ReturnValue->Val->Integer = BabyPosixGettimeofday(
        (struct BabyPosixTimeval *)Param[0]->Val->Pointer);
}

struct BabyPosixTimespec {
    long long tv_sec;
    long tv_nsec;
};

static void BabyPosixClockGettimeCall(struct ParseState *Parser,
    struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    struct BabyPosixTimespec *out = (struct BabyPosixTimespec *)Param[1]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (out == NULL) {
        errno = EFAULT; ReturnValue->Val->Integer = -1; return;
    }
#ifdef WIN32
    {
        struct BabyPosixTimeval tv;
        if (BabyPosixGettimeofday(&tv) != 0) {
            ReturnValue->Val->Integer = -1; return;
        }
        out->tv_sec = tv.tv_sec;
        out->tv_nsec = (long)(tv.tv_usec * 1000LL);
        ReturnValue->Val->Integer = 0;
    }
#else
    {
        struct timespec ts;
        if (clock_gettime((clockid_t)Param[0]->Val->Integer, &ts) != 0) {
            ReturnValue->Val->Integer = -1; return;
        }
        out->tv_sec = (long long)ts.tv_sec;
        out->tv_nsec = (long)ts.tv_nsec;
        ReturnValue->Val->Integer = 0;
    }
#endif
}

static void BabyPosixNanosleepCall(struct ParseState *Parser,
    struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    struct BabyPosixTimespec *req = (struct BabyPosixTimespec *)Param[0]->Val->Pointer;
    struct BabyPosixTimespec *rem = (struct BabyPosixTimespec *)Param[1]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (req == NULL) {
        errno = EFAULT; ReturnValue->Val->Integer = -1; return;
    }
#ifdef WIN32
    (void)rem;
    errno = ENOSYS; ReturnValue->Val->Integer = -1;
#else
    {
        struct timespec nreq, nrem;
        int gil, rc;
        nreq.tv_sec = (time_t)req->tv_sec;
        nreq.tv_nsec = req->tv_nsec;
        gil = BabycGuestGIL_UnlockIfHeld();
        rc = nanosleep(&nreq, rem ? &nrem : NULL);
        BabycGuestGIL_RelockIf(gil);
        if (rc != 0 && rem != NULL) {
            rem->tv_sec = (long long)nrem.tv_sec;
            rem->tv_nsec = (long)nrem.tv_nsec;
        }
        ReturnValue->Val->Integer = rc;
    }
#endif
}

const char BabyPosixTimeDefs[] =
    "typedef long long babyposix_time_t;\n"
    "struct timeval { babyposix_time_t tv_sec; babyposix_time_t tv_usec; };\n"
    "struct timespec { long long tv_sec; long tv_nsec; };\n"
    "typedef int clockid_t;\n"
    "#define CLOCK_REALTIME 0\n"
    "#define CLOCK_MONOTONIC 1\n"
    "#define CLOCK_PROCESS_CPUTIME_ID 2\n"
    "#define CLOCK_THREAD_CPUTIME_ID 3\n"
    "#define CLOCK_MONOTONIC_RAW 4\n";

struct LibraryFunction BabyPosixTimeFunctions[] = {
    { BabyPosixGettimeofdayCall, "int gettimeofday(struct timeval *, void *);" },
    { BabyPosixClockGettimeCall, "int clock_gettime(int, struct timespec *);" },
    { BabyPosixNanosleepCall, "int nanosleep(struct timespec *, struct timespec *);" },
    { NULL, NULL }
};

void BabyPosixTimeSetupFunc(Picoc *pc)
{
    /* The visible struct definition is parsed from BabyPosixTimeDefs. */
    (void)pc;
}

struct BabyPosixTms {
    long tms_utime;
    long tms_stime;
    long tms_cutime;
    long tms_cstime;
};

static void BabyPosixTimesCall(struct ParseState *Parser,
    struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    struct BabyPosixTms *out = (struct BabyPosixTms *)Param[0]->Val->Pointer;
    (void)Parser; (void)NumArgs;
    if (out == NULL) {
        errno = EFAULT; ReturnValue->Val->Integer = -1; return;
    }
#ifdef WIN32
    memset(out, 0, sizeof(*out));
    ReturnValue->Val->Integer = 0;
#else
    {
        struct tms t;
        clock_t c = times(&t);
        if (c == (clock_t)-1) {
            ReturnValue->Val->Integer = -1; return;
        }
        out->tms_utime = (long)t.tms_utime;
        out->tms_stime = (long)t.tms_stime;
        out->tms_cutime = (long)t.tms_cutime;
        out->tms_cstime = (long)t.tms_cstime;
        ReturnValue->Val->Integer = (long)c;
    }
#endif
}

const char BabyPosixTimesDefs[] =
    "struct tms { long tms_utime; long tms_stime; long tms_cutime; long tms_cstime; };\n";

struct LibraryFunction BabyPosixTimesFunctions[] = {
    { BabyPosixTimesCall, "long times(struct tms *);" },
    { NULL, NULL }
};

void BabyPosixTimesSetupFunc(Picoc *pc) { (void)pc; }
