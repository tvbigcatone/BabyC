/* BabyPOSIX pipe2/dup3 CLOEXEC contracts. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <errno.h>

int main(void)
{
    int p[2];
    int rc;

    if (pipe2(p, 0) != 0) { puts("pipe2 0"); return 1; }
    if (fcntl(p[0], F_GETFD) != 0 || fcntl(p[1], F_GETFD) != 0) {
        puts("pipe2 0 cloexec"); return 2;
    }
    close(p[0]); close(p[1]);

    if (pipe2(p, O_CLOEXEC) != 0) { puts("pipe2 CLOEXEC"); return 3; }
    if (fcntl(p[0], F_GETFD) != FD_CLOEXEC || fcntl(p[1], F_GETFD) != FD_CLOEXEC) {
        puts("pipe2 CLOEXEC flags"); return 4;
    }
    if (dup3(p[0], 40, 0) != 40) { puts("dup3"); return 5; }
    if (fcntl(40, F_GETFD) != 0) { puts("dup3 clears cloexec"); return 6; }
    if (dup3(p[0], 40, O_CLOEXEC) != 40) { puts("dup3 cloexec"); return 7; }
    if (fcntl(40, F_GETFD) != FD_CLOEXEC) { puts("dup3 sets cloexec"); return 8; }
    errno = 0;
    rc = dup3(p[0], p[0], 0);
    if (rc != -1) { puts("dup3 same fd"); return 9; }
    close(40); close(p[0]); close(p[1]);
    puts("BabyPOSIX pipe2/dup3: ok");
    return 0;
}
