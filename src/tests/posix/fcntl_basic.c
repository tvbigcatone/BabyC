#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "babyposix-fcntl.tmp";
    const char initial[] = "abc";
    char buf[8] = {0};
    int fd, copy, flags, rc;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd < 3) { puts("FAIL: open"); return 1; }
    if (write(fd, (void *)initial, 3) != 3) {
        close(fd); unlink(path); puts("FAIL: initial write"); return 2;
    }
    if (fcntl(-1, F_GETFL) != -1 || fcntl(fd, 999) != -1) {
        close(fd); unlink(path); puts("FAIL: invalid fcntl arguments"); return 3;
    }
    flags = fcntl(fd, F_GETFL);
    if (flags < 0 || (flags & 3) != O_RDWR) {
        close(fd); unlink(path); puts("FAIL: F_GETFL access mode"); return 4;
    }
    if (fcntl(-1, F_GETFD) != -1 || fcntl(fd, F_GETFD) != 0 ||
        fcntl(fd, F_SETFD, FD_CLOEXEC) != 0 ||
        fcntl(fd, F_GETFD) != FD_CLOEXEC) {
        close(fd); unlink(path); puts("FAIL: F_GETFD/F_SETFD"); return 13;
    }
    if (fcntl(fd, F_SETFD, 2) != -1) {
        close(fd); unlink(path); puts("FAIL: unsupported descriptor flag accepted"); return 15;
    }
    copy = dup(fd);
    if (copy < 3) {
        close(fd); unlink(path); puts("FAIL: dup"); return 5;
    }
    if (fcntl(copy, F_GETFD) != 0) {
        close(copy); close(fd); unlink(path); puts("FAIL: dup should clear FD_CLOEXEC"); return 14;
    }
    if (dup2(fd, 40) != 40 || fcntl(40, F_SETFD, FD_CLOEXEC) != 0 ||
        dup2(copy, 40) != 40 || fcntl(40, F_GETFD) != 0) {
        close(40); close(copy); close(fd); unlink(path);
        puts("FAIL: dup2 should clear FD_CLOEXEC"); return 16;
    }
    if (close(40) != 0) {
        close(copy); close(fd); unlink(path); puts("FAIL: close dup2 target"); return 17;
    }

#if BABYPOSIX_FCNTL_SETFL_SUPPORTED
    rc = fcntl(fd, F_SETFL, O_NONBLOCK);
    if (rc != 0 || (fcntl(fd, F_GETFL) & O_NONBLOCK) == 0) {
        close(copy); close(fd); unlink(path); puts("FAIL: F_SETFL O_NONBLOCK"); return 18;
    }
    rc = fcntl(fd, F_SETFL, 0);
    if (rc != 0 || (fcntl(fd, F_GETFL) & O_NONBLOCK) != 0) {
        close(copy); close(fd); unlink(path); puts("FAIL: F_SETFL clear O_NONBLOCK"); return 19;
    }
    rc = fcntl(fd, F_SETFL, flags | O_APPEND);
    if (rc != 0) {
        close(copy); close(fd); unlink(path); puts("FAIL: F_SETFL append"); return 6;
    }
    flags = fcntl(copy, F_GETFL);
    if (flags < 0 || !(flags & O_APPEND)) {
        close(copy); close(fd); unlink(path); puts("FAIL: shared append flag"); return 7;
    }
    if (lseek(fd, 0, SEEK_SET) != 0 || write(fd, (void *)"X", 1) != 1) {
        close(copy); close(fd); unlink(path); puts("FAIL: append write"); return 8;
    }
    if (lseek(fd, 0, SEEK_SET) != 0 || read(fd, buf, 4) != 4 ||
        memcmp(buf, "abcX", 4) != 0) {
        close(copy); close(fd); unlink(path); puts("FAIL: append semantics"); return 9;
    }
#else
    rc = fcntl(fd, F_SETFL, flags | O_APPEND);
    if (rc != -1) {
        close(copy); close(fd); unlink(path); puts("FAIL: unsupported F_SETFL should fail"); return 10;
    }
#endif

    if (close(copy) != 0 || close(fd) != 0) {
        unlink(path); puts("FAIL: close"); return 11;
    }
    if (unlink(path) != 0) { puts("FAIL: unlink"); return 12; }
    puts("BabyPOSIX fcntl: ok");
    return 0;
}
