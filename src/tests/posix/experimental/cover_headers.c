#include <paths.h>
#include <sysexits.h>
#include <langinfo.h>
#include <syslog.h>
#include <glob.h>
#include <sys/mman.h>
#include <sys/uio.h>
#include <sys/statvfs.h>
#include <wchar.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    glob_t g;
    struct statvfs sv;
    struct iovec iov;
    char buf[8];
    int fd, n;
    void *p;
    wchar_t w[4];
    jmp_buf env;
    char *cs;
    if (EX_OK != 0 || EX_USAGE != 64) { puts("sysexits"); return 1; }
    if (_PATH_DEVNULL[0] != '/') { puts("paths"); return 2; }
    cs = nl_langinfo(CODESET);
    if (cs == 0) { puts("langinfo"); return 3; }
    (void)LOG_INFO; (void)LOG_USER;
    memset(&g, 0, sizeof(g));
    if (glob("*", 0, 0, &g) != 0 || g.gl_pathc < 1) { puts("glob"); return 4; }
    globfree(&g);
    memset(&g, 0, sizeof(g)); g.gl_offs = 2;
    if (glob("/etc/passwd", GLOB_DOOFFS, 0, &g) != 0 || g.gl_pathc != 1 ||
        g.gl_pathv[0] != 0 || g.gl_pathv[1] != 0 || g.gl_pathv[2] == 0) {
        puts("glob dooffs"); return 14;
    }
    globfree(&g);
    memset(&g, 0, sizeof(g));
    if (glob("/etc/passwd", 0, 0, &g) != 0) { puts("glob2"); return 15; }
    if (glob("/etc/group", GLOB_APPEND, 0, &g) != 0 || g.gl_pathc != 2) {
        puts("glob append"); return 16;
    }
    globfree(&g);
    fd = open("bp_s8_uio.tmp", O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) { puts("open"); return 5; }
    memcpy(buf, "abcd", 4); iov.iov_base = buf; iov.iov_len = 4;
    n = writev(fd, &iov, 1);
    if (n != 4) { puts("writev"); close(fd); return 6; }
    if (lseek(fd, 0, 0) != 0) { puts("lseek"); close(fd); return 7; }
    memset(buf, 0, sizeof(buf)); iov.iov_base = buf; iov.iov_len = 4;
    n = readv(fd, &iov, 1);
    if (n != 4 || buf[0] != 'a') { puts("readv"); close(fd); return 8; }
    close(fd); unlink("bp_s8_uio.tmp");
    p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == 0 || p == (void *)-1) { puts("mmap"); return 9; }
    ((char *)p)[0] = 42;
    if (munmap(p, 4096) != 0) { puts("munmap"); return 10; }
    if (statvfs(".", &sv) != 0 || sv.f_bsize == 0) { puts("statvfs"); return 11; }
    w[0] = 'a'; w[1] = 'b'; w[2] = 0;
    if (wcslen(w) != 2) { puts("wcslen"); return 12; }
    if (setjmp(env) != 0) { puts("setjmp"); return 13; }
    puts("BabyPOSIX cover headers: ok"); return 0;
}
