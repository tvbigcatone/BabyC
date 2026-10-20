/* BabyPOSIX pread/pwrite leave the file offset unchanged. */
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char *path = "bp_s10_pread.tmp";
    char buf[8];
    int fd, n, fl, copy;

    fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0600);
    if (fd < 3) { puts("open"); return 1; }
    if (write(fd, "abcdefgh", 8) != 8) { close(fd); unlink(path); puts("write"); return 2; }
    if (lseek(fd, 0, SEEK_SET) != 0) { close(fd); unlink(path); puts("lseek"); return 3; }
    memset(buf, 0, sizeof(buf));
    n = pread(fd, buf, 4, 2);
    if (n != 4 || memcmp(buf, "cdef", 4) != 0) {
        close(fd); unlink(path); puts("pread"); return 4;
    }
    if (lseek(fd, 0, SEEK_CUR) != 0) {
        close(fd); unlink(path); puts("pread moved offset"); return 5;
    }
    n = pwrite(fd, "XY", 2, 0);
    if (n != 2) { close(fd); unlink(path); puts("pwrite"); return 6; }
    if (lseek(fd, 0, SEEK_CUR) != 0) {
        close(fd); unlink(path); puts("pwrite moved offset"); return 7;
    }
    if (pread(fd, buf, 2, 0) != 2 || buf[0] != 'X' || buf[1] != 'Y') {
        close(fd); unlink(path); puts("pwrite data"); return 8;
    }
    if (pread(-1, buf, 1, 0) != -1) {
        close(fd); unlink(path); puts("pread badfd"); return 9;
    }
    if (fdatasync(fd) != 0) {
        close(fd); unlink(path); puts("fdatasync"); return 10;
    }
    copy = fcntl(fd, F_DUPFD, 20);
    if (copy < 20) {
        close(fd); unlink(path); puts("F_DUPFD"); return 11;
    }
    if (fcntl(copy, F_GETFD) != 0) {
        close(copy); close(fd); unlink(path); puts("F_DUPFD cloexec"); return 12;
    }
    fl = fcntl(fd, F_DUPFD_CLOEXEC, 30);
    if (fl < 30 || fcntl(fl, F_GETFD) != FD_CLOEXEC) {
        close(fl); close(copy); close(fd); unlink(path); puts("F_DUPFD_CLOEXEC"); return 13;
    }
    close(fl); close(copy); close(fd); unlink(path);
    puts("BabyPOSIX pread/pwrite: ok");
    return 0;
}
