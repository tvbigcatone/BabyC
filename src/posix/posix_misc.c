/* BabyPOSIX leftovers used by coreutils compile: getopt, libgen, utime,
 * uname, ioctl, termios. Educational subset; host-backed where portable. */
#include "posix_misc.h"
#include "posix_fd.h"
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#ifdef WIN32
# include <io.h>
#else
# include <unistd.h>
# include <getopt.h>
# include <libgen.h>
# include <utime.h>
# include <sys/utsname.h>
# include <sys/ioctl.h>
# include <termios.h>
#endif

/* ---- getopt ---- */

static int BabyPosixOpterr = 1;
static int BabyPosixOptind = 1;
static int BabyPosixOptopt = 0;
static char *BabyPosixOptarg;

static void BabyPosixGetoptCall(struct ParseState *p, struct Value *r,
                                struct Value **a, int n)
{
    int argc = (int)a[0]->Val->Integer;
    char **argv = (char **)a[1]->Val->Pointer;
    const char *optstring = (const char *)a[2]->Val->Pointer;
    (void)p; (void)n;
    if (argv == NULL || optstring == NULL) {
        errno = EFAULT; r->Val->Integer = -1; return;
    }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    optind = BabyPosixOptind;
    opterr = BabyPosixOpterr;
    optopt = BabyPosixOptopt;
    r->Val->Integer = getopt(argc, argv, optstring);
    BabyPosixOptind = optind;
    BabyPosixOpterr = opterr;
    BabyPosixOptopt = optopt;
    BabyPosixOptarg = optarg;
#endif
}

static void BabyPosixGetoptLongCall(struct ParseState *p, struct Value *r,
                                    struct Value **a, int n)
{
    int argc = (int)a[0]->Val->Integer;
    char **argv = (char **)a[1]->Val->Pointer;
    const char *optstring = (const char *)a[2]->Val->Pointer;
    (void)p; (void)n;
    if (argv == NULL) {
        errno = EFAULT; r->Val->Integer = -1; return;
    }
#ifdef WIN32
    (void)optstring; (void)a;
    errno = ENOSYS; r->Val->Integer = -1;
#else
    optind = BabyPosixOptind;
    opterr = BabyPosixOpterr;
    optopt = BabyPosixOptopt;
    r->Val->Integer = getopt_long(argc, argv, optstring ? optstring : "",
        (const struct option *)a[3]->Val->Pointer,
        (int *)a[4]->Val->Pointer);
    BabyPosixOptind = optind;
    BabyPosixOpterr = opterr;
    BabyPosixOptopt = optopt;
    BabyPosixOptarg = optarg;
#endif
}

const char BabyPosixGetoptDefs[] =
    "struct option {\n"
    "    char *name;\n"
    "    int has_arg;\n"
    "    int *flag;\n"
    "    int val;\n"
    "};\n"
    "#define no_argument 0\n"
    "#define required_argument 1\n"
    "#define optional_argument 2\n";

struct LibraryFunction BabyPosixGetoptFunctions[] = {
    { BabyPosixGetoptCall, "int getopt(int, char **, char *);" },
    { BabyPosixGetoptLongCall, "int getopt_long(int, char **, char *, struct option *, int *);" },
    { NULL, NULL }
};

void BabyPosixGetoptSetupFunc(Picoc *pc)
{
    VariableDefinePlatformVar(pc, NULL, "opterr", &pc->IntType,
        (union AnyValue *)&BabyPosixOpterr, TRUE);
    VariableDefinePlatformVar(pc, NULL, "optind", &pc->IntType,
        (union AnyValue *)&BabyPosixOptind, TRUE);
    VariableDefinePlatformVar(pc, NULL, "optopt", &pc->IntType,
        (union AnyValue *)&BabyPosixOptopt, TRUE);
    VariableDefinePlatformVar(pc, NULL, "optarg", pc->CharPtrType,
        (union AnyValue *)&BabyPosixOptarg, TRUE);
}

/* ---- libgen ---- */

static char BabyPosixDirnameBuf[4096];
static char BabyPosixBasenameBuf[4096];

static void BabyPosixDirnameCall(struct ParseState *p, struct Value *r,
                                 struct Value **a, int n)
{
    char *path = (char *)a[0]->Val->Pointer;
    (void)p; (void)n;
#ifdef WIN32
    {
        char *slash;
        if (path == NULL) { r->Val->Pointer = (void *)"."; return; }
        strncpy(BabyPosixDirnameBuf, path, sizeof(BabyPosixDirnameBuf) - 1);
        BabyPosixDirnameBuf[sizeof(BabyPosixDirnameBuf) - 1] = 0;
        slash = strrchr(BabyPosixDirnameBuf, '/');
        if (slash == NULL) slash = strrchr(BabyPosixDirnameBuf, '\\');
        if (slash == NULL) { r->Val->Pointer = (void *)"."; return; }
        if (slash == BabyPosixDirnameBuf) { BabyPosixDirnameBuf[1] = 0; }
        else *slash = 0;
        r->Val->Pointer = BabyPosixDirnameBuf;
    }
#else
    if (path == NULL) {
        r->Val->Pointer = dirname(NULL);
        return;
    }
    strncpy(BabyPosixDirnameBuf, path, sizeof(BabyPosixDirnameBuf) - 1);
    BabyPosixDirnameBuf[sizeof(BabyPosixDirnameBuf) - 1] = 0;
    r->Val->Pointer = dirname(BabyPosixDirnameBuf);
#endif
}

static void BabyPosixBasenameCall(struct ParseState *p, struct Value *r,
                                  struct Value **a, int n)
{
    char *path = (char *)a[0]->Val->Pointer;
    (void)p; (void)n;
#ifdef WIN32
    {
        char *slash;
        if (path == NULL) { r->Val->Pointer = (void *)"."; return; }
        strncpy(BabyPosixBasenameBuf, path, sizeof(BabyPosixBasenameBuf) - 1);
        BabyPosixBasenameBuf[sizeof(BabyPosixBasenameBuf) - 1] = 0;
        slash = strrchr(BabyPosixBasenameBuf, '/');
        if (slash == NULL) slash = strrchr(BabyPosixBasenameBuf, '\\');
        r->Val->Pointer = slash ? slash + 1 : BabyPosixBasenameBuf;
    }
#else
    if (path == NULL) {
        r->Val->Pointer = basename(NULL);
        return;
    }
    strncpy(BabyPosixBasenameBuf, path, sizeof(BabyPosixBasenameBuf) - 1);
    BabyPosixBasenameBuf[sizeof(BabyPosixBasenameBuf) - 1] = 0;
    r->Val->Pointer = basename(BabyPosixBasenameBuf);
#endif
}

const char BabyPosixLibgenDefs[] = "";

struct LibraryFunction BabyPosixLibgenFunctions[] = {
    { BabyPosixDirnameCall, "char *dirname(char *);" },
    { BabyPosixBasenameCall, "char *basename(char *);" },
    { NULL, NULL }
};

void BabyPosixLibgenSetupFunc(Picoc *pc) { (void)pc; }

/* ---- utime ---- */

struct BabyPosixUtimbuf {
    long actime;
    long modtime;
};

static void BabyPosixUtimeCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    const char *path = (const char *)a[0]->Val->Pointer;
    struct BabyPosixUtimbuf *times = (struct BabyPosixUtimbuf *)a[1]->Val->Pointer;
    (void)p; (void)n;
    if (path == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct utimbuf ub;
        if (times == NULL) {
            r->Val->Integer = utime(path, NULL);
            return;
        }
        ub.actime = (time_t)times->actime;
        ub.modtime = (time_t)times->modtime;
        r->Val->Integer = utime(path, &ub);
    }
#endif
}

const char BabyPosixUtimeDefs[] =
    "struct utimbuf { long actime; long modtime; };\n";

struct LibraryFunction BabyPosixUtimeFunctions[] = {
    { BabyPosixUtimeCall, "int utime(char *, struct utimbuf *);" },
    { NULL, NULL }
};

void BabyPosixUtimeSetupFunc(Picoc *pc) { (void)pc; }

/* ---- uname ---- */

struct BabyPosixUtsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
};

static void BabyPosixUnameCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    struct BabyPosixUtsname *out = (struct BabyPosixUtsname *)a[0]->Val->Pointer;
    (void)p; (void)n;
    if (out == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
#ifdef WIN32
    memset(out, 0, sizeof(*out));
    strncpy(out->sysname, "Windows", 64);
    strncpy(out->nodename, "localhost", 64);
    strncpy(out->release, "unknown", 64);
    strncpy(out->version, "unknown", 64);
    strncpy(out->machine, "x86_64", 64);
    r->Val->Integer = 0;
#else
    {
        struct utsname un;
        if (uname(&un) != 0) { r->Val->Integer = -1; return; }
        memset(out, 0, sizeof(*out));
        memcpy(out->sysname, un.sysname, sizeof(out->sysname));
        out->sysname[sizeof(out->sysname) - 1] = '\0';
        memcpy(out->nodename, un.nodename, sizeof(out->nodename));
        out->nodename[sizeof(out->nodename) - 1] = '\0';
        memcpy(out->release, un.release, sizeof(out->release));
        out->release[sizeof(out->release) - 1] = '\0';
        memcpy(out->version, un.version, sizeof(out->version));
        out->version[sizeof(out->version) - 1] = '\0';
        memcpy(out->machine, un.machine, sizeof(out->machine));
        out->machine[sizeof(out->machine) - 1] = '\0';
        r->Val->Integer = 0;
    }
#endif
}

const char BabyPosixUtsnameDefs[] =
    "struct utsname {\n"
    "    char sysname[65];\n"
    "    char nodename[65];\n"
    "    char release[65];\n"
    "    char version[65];\n"
    "    char machine[65];\n"
    "};\n";

struct LibraryFunction BabyPosixUtsnameFunctions[] = {
    { BabyPosixUnameCall, "int uname(struct utsname *);" },
    { NULL, NULL }
};

void BabyPosixUtsnameSetupFunc(Picoc *pc) { (void)pc; }

/* ---- ioctl (TIOCGWINSZ subset) ---- */

#define BP_TIOCGWINSZ 0x5413
#define BP_TIOCSWINSZ 0x5414

struct BabyPosixWinsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

static void BabyPosixIoctlCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    int fd = (int)a[0]->Val->Integer;
    int req = (int)a[1]->Val->Integer;
    void *arg = n >= 3 ? a[2]->Val->Pointer : NULL;
    int native;
    (void)p;
    native = BabyPosixFdNativeOf(fd);
    if (native < 0) { r->Val->Integer = -1; return; }
#ifdef WIN32
    (void)req; (void)arg;
    errno = ENOSYS; r->Val->Integer = -1;
#else
    if (req == BP_TIOCGWINSZ || req == BP_TIOCSWINSZ) {
        r->Val->Integer = ioctl(native, (unsigned long)req, arg);
        return;
    }
    errno = ENOTTY;
    r->Val->Integer = -1;
#endif
}

const char BabyPosixIoctlDefs[] =
    "struct winsize {\n"
    "    unsigned short ws_row;\n"
    "    unsigned short ws_col;\n"
    "    unsigned short ws_xpixel;\n"
    "    unsigned short ws_ypixel;\n"
    "};\n"
    "#define TIOCGWINSZ 0x5413\n"
    "#define TIOCSWINSZ 0x5414\n";

struct LibraryFunction BabyPosixIoctlFunctions[] = {
    { BabyPosixIoctlCall, "int ioctl(int, int, ...);" },
    { NULL, NULL }
};

void BabyPosixIoctlSetupFunc(Picoc *pc) { (void)pc; }

/* ---- termios: isatty already in unistd; tcgetattr/tcsetattr stubs ---- */

struct BabyPosixTermios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_cc[32];
    unsigned int c_ispeed;
    unsigned int c_ospeed;
};

static void BabyPosixTcgetattrCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    int native;
    struct BabyPosixTermios *out = (struct BabyPosixTermios *)a[1]->Val->Pointer;
    (void)p; (void)n;
    if (out == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
    native = BabyPosixFdNativeOf((int)a[0]->Val->Integer);
    if (native < 0) { r->Val->Integer = -1; return; }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct termios t;
        if (tcgetattr(native, &t) != 0) { r->Val->Integer = -1; return; }
        memset(out, 0, sizeof(*out));
        out->c_iflag = (unsigned int)t.c_iflag;
        out->c_oflag = (unsigned int)t.c_oflag;
        out->c_cflag = (unsigned int)t.c_cflag;
        out->c_lflag = (unsigned int)t.c_lflag;
        memcpy(out->c_cc, t.c_cc, 32 < NCCS ? 32 : NCCS);
        r->Val->Integer = 0;
    }
#endif
}

static void BabyPosixTcsetattrCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    int native;
    struct BabyPosixTermios *in = (struct BabyPosixTermios *)a[2]->Val->Pointer;
    (void)p; (void)n;
    if (in == NULL) { errno = EFAULT; r->Val->Integer = -1; return; }
    native = BabyPosixFdNativeOf((int)a[0]->Val->Integer);
    if (native < 0) { r->Val->Integer = -1; return; }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        struct termios t;
        memset(&t, 0, sizeof(t));
        t.c_iflag = in->c_iflag;
        t.c_oflag = in->c_oflag;
        t.c_cflag = in->c_cflag;
        t.c_lflag = in->c_lflag;
        memcpy(t.c_cc, in->c_cc, 32 < NCCS ? 32 : NCCS);
        r->Val->Integer = tcsetattr(native, (int)a[1]->Val->Integer, &t);
    }
#endif
}

const char BabyPosixTermiosDefs[] =
    "struct termios {\n"
    "    unsigned int c_iflag;\n"
    "    unsigned int c_oflag;\n"
    "    unsigned int c_cflag;\n"
    "    unsigned int c_lflag;\n"
    "    unsigned char c_cc[32];\n"
    "    unsigned int c_ispeed;\n"
    "    unsigned int c_ospeed;\n"
    "};\n"
    "#define TCSANOW 0\n"
    "#define TCSADRAIN 1\n"
    "#define TCSAFLUSH 2\n"
    "#define ECHO 8\n"
    "#define ICANON 2\n"
    "#define ISIG 1\n";

struct LibraryFunction BabyPosixTermiosFunctions[] = {
    { BabyPosixTcgetattrCall, "int tcgetattr(int, struct termios *);" },
    { BabyPosixTcsetattrCall, "int tcsetattr(int, int, struct termios *);" },
    { NULL, NULL }
};

void BabyPosixTermiosSetupFunc(Picoc *pc) { (void)pc; }
