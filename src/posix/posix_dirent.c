/* BabyPOSIX directory iteration with a stable, deliberately small ABI.
 * The interpreted struct dirent is not a native host struct: each backend
 * copies the entry name into this module's stable result object.
 */
#include "posix_dirent.h"
#include "posix_fd.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef WIN32
# include <windows.h>
# include <direct.h>
#else
# include <dirent.h>
# include <sys/types.h>
# include <fcntl.h>
# include <unistd.h>
#endif

#define BABYPOSIX_DIRENT_NAME_MAX 255

struct BabyPosixDirentValue {
    long d_ino;
    unsigned char d_type;
    char d_name[BABYPOSIX_DIRENT_NAME_MAX + 1];
};

struct BabyPosixDIR {
#ifdef WIN32
    HANDLE find;
    WIN32_FIND_DATAA data;
    int first_pending;
    char *pattern;
#else
    DIR *native;
#endif
    int guest_fd;
    struct BabyPosixDirentValue entry;
};

static void BabyPosixCopyName(struct BabyPosixDIR *dir, const char *name)
{
    size_t n = strlen(name);
    if (n > BABYPOSIX_DIRENT_NAME_MAX) n = BABYPOSIX_DIRENT_NAME_MAX;
    memcpy(dir->entry.d_name, name, n);
    dir->entry.d_name[n] = '\0';
}

static unsigned char BabyPosixWinDirType(unsigned long attr)
{
#ifdef WIN32
    if (attr & FILE_ATTRIBUTE_DIRECTORY) return 4;
    if (attr & FILE_ATTRIBUTE_REPARSE_POINT) return 10;
    return 8;
#else
    (void)attr;
    return 0;
#endif
}

static void BabyPosixOpendirCall(struct ParseState *p, struct Value *r,
                                 struct Value **a, int n)
{
    struct BabyPosixDIR *dir;
    const char *path;
    (void)p; (void)n;
    path = (const char *)a[0]->Val->Pointer;
    if (path == NULL) { errno = EFAULT; r->Val->Pointer = NULL; return; }
    if (path[0] == '\0') { errno = EINVAL; r->Val->Pointer = NULL; return; }
    dir = (struct BabyPosixDIR *)calloc(1, sizeof(*dir));
    if (dir == NULL) { errno = ENOMEM; r->Val->Pointer = NULL; return; }
#ifdef WIN32
    {
        size_t len = strlen(path);
        int has_sep = len > 0 && (path[len - 1] == '/' || path[len - 1] == '\\');
        dir->pattern = (char *)malloc(len + (has_sep ? 2 : 3));
        if (dir->pattern == NULL) { free(dir); errno = ENOMEM; r->Val->Pointer = NULL; return; }
        memcpy(dir->pattern, path, len);
        if (!has_sep) dir->pattern[len++] = '\\';
        dir->pattern[len++] = '*';
        dir->pattern[len] = '\0';
        dir->find = FindFirstFileA(dir->pattern, &dir->data);
        if (dir->find == INVALID_HANDLE_VALUE) {
            DWORD e = GetLastError();
            free(dir->pattern); free(dir);
            errno = (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) ? ENOENT : EACCES;
            r->Val->Pointer = NULL; return;
        }
        dir->first_pending = 1;
        dir->guest_fd = -1;
    }
#else
    dir->native = opendir(path);
    if (dir->native == NULL) { free(dir); r->Val->Pointer = NULL; return; }
    dir->guest_fd = -1;
#endif
    r->Val->Pointer = dir;
}

static void BabyPosixClosedirCall(struct ParseState *p, struct Value *r,
                                  struct Value **a, int n)
{
    struct BabyPosixDIR *dir = (struct BabyPosixDIR *)a[0]->Val->Pointer;
    int rc = 0;
    (void)p; (void)n;
    if (dir == NULL) { errno = EBADF; r->Val->Integer = -1; return; }
#ifdef WIN32
    if (dir->find != INVALID_HANDLE_VALUE && !FindClose(dir->find)) rc = -1;
    free(dir->pattern);
#else
    rc = closedir(dir->native);
#endif
    if (dir->guest_fd >= 0)
        BabyPosixFdRelease(dir->guest_fd);
    free(dir);
    r->Val->Integer = rc;
}

static void BabyPosixReaddirCall(struct ParseState *p, struct Value *r,
                                 struct Value **a, int n)
{
    struct BabyPosixDIR *dir = (struct BabyPosixDIR *)a[0]->Val->Pointer;
    (void)p; (void)n;
    if (dir == NULL) { errno = EBADF; r->Val->Pointer = NULL; return; }
#ifdef WIN32
    if (dir->first_pending) dir->first_pending = 0;
    else if (!FindNextFileA(dir->find, &dir->data)) {
        DWORD e = GetLastError();
        if (e != ERROR_NO_MORE_FILES) errno = EIO;
        r->Val->Pointer = NULL; return;
    }
    BabyPosixCopyName(dir, dir->data.cFileName);
    {
        unsigned long h = 5381UL;
        const unsigned char *s = (const unsigned char *)dir->data.cFileName;
        while (*s != 0)
            h = h * 33UL + *s++;
        dir->entry.d_ino = (long)h;
    }
    dir->entry.d_type = BabyPosixWinDirType(dir->data.dwFileAttributes);
#else
    {
        struct dirent *native = readdir(dir->native);
        if (native == NULL) { r->Val->Pointer = NULL; return; }
        BabyPosixCopyName(dir, native->d_name);
        dir->entry.d_ino = (long)native->d_ino;
#ifdef DT_UNKNOWN
        dir->entry.d_type = native->d_type;
#else
        dir->entry.d_type = 0;
#endif
    }
#endif
    r->Val->Pointer = &dir->entry;
}

static void BabyPosixRewinddirCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    struct BabyPosixDIR *dir = (struct BabyPosixDIR *)a[0]->Val->Pointer;
    (void)p; (void)r; (void)n;
    if (dir == NULL) { errno = EBADF; return; }
#ifdef WIN32
    if (dir->find != INVALID_HANDLE_VALUE) FindClose(dir->find);
    dir->find = FindFirstFileA(dir->pattern, &dir->data);
    if (dir->find == INVALID_HANDLE_VALUE) { dir->first_pending = 0; errno = EIO; }
    else dir->first_pending = 1;
#else
    rewinddir(dir->native);
#endif
}

static void BabyPosixDirfdCall(struct ParseState *p, struct Value *r,
                               struct Value **a, int n)
{
    struct BabyPosixDIR *dir = (struct BabyPosixDIR *)a[0]->Val->Pointer;
    (void)p; (void)n;
    if (dir == NULL) { errno = EBADF; r->Val->Integer = -1; return; }
    if (dir->guest_fd >= 0) {
        if (BabyPosixFdNativeOf(dir->guest_fd) >= 0) {
            r->Val->Integer = dir->guest_fd;
            return;
        }
        dir->guest_fd = -1;
    }
#ifdef WIN32
    errno = ENOSYS; r->Val->Integer = -1;
#else
    {
        int native = dirfd(dir->native);
        int guest;
        if (native < 0) { r->Val->Integer = -1; return; }
        guest = BabyPosixFdAllocate(native, BP_FD_READ, 0);
        if (guest < 0) { r->Val->Integer = -1; return; }
        dir->guest_fd = guest;
        r->Val->Integer = guest;
    }
#endif
}

static void BabyPosixFdopendirCall(struct ParseState *p, struct Value *r,
                                   struct Value **a, int n)
{
    int guest = (int)a[0]->Val->Integer;
    (void)p; (void)n;
#ifdef WIN32
    (void)guest;
    errno = ENOSYS; r->Val->Pointer = NULL;
#else
    {
        struct BabyPosixDIR *dir;
        int native = BabyPosixFdNativeOf(guest);
        DIR *d;
        if (native < 0) { r->Val->Pointer = NULL; return; }
        d = fdopendir(native);
        if (d == NULL) { r->Val->Pointer = NULL; return; }
        dir = (struct BabyPosixDIR *)calloc(1, sizeof(*dir));
        if (dir == NULL) {
            int saved = errno;
            closedir(d);
            errno = saved;
            r->Val->Pointer = NULL;
            return;
        }
        BabyPosixFdDisown(guest);
        dir->native = d;
        dir->guest_fd = guest;
        r->Val->Pointer = dir;
    }
#endif
}

const char BabyPosixDirentDefs[] =
    "typedef struct __babyposix_DIR { void *opaque; } DIR;\n"
    "struct dirent { long d_ino; unsigned char d_type; char d_name[256]; };\n"
    "#define DT_UNKNOWN 0\n"
    "#define DT_FIFO 1\n"
    "#define DT_CHR 2\n"
    "#define DT_DIR 4\n"
    "#define DT_BLK 6\n"
    "#define DT_REG 8\n"
    "#define DT_LNK 10\n"
    "#define DT_SOCK 12\n";

struct LibraryFunction BabyPosixDirentFunctions[] = {
    { BabyPosixOpendirCall, "DIR *opendir(char *);" },
    { BabyPosixClosedirCall, "int closedir(DIR *);" },
    { BabyPosixReaddirCall, "struct dirent *readdir(DIR *);" },
    { BabyPosixRewinddirCall, "void rewinddir(DIR *);" },
    { BabyPosixDirfdCall, "int dirfd(DIR *);" },
    { BabyPosixFdopendirCall, "DIR *fdopendir(int);" },
    { NULL, NULL }
};

void BabyPosixDirentSetupFunc(Picoc *pc) { (void)pc; }
