/* BabyPOSIX msync/mprotect and utimensat. */
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <sys/time.h>

int main(void)
{
    void *p;
    struct timespec ts[2];
    const char *path = "bp_s10_utimens.tmp";
    int fd;

    p = mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == 0 || p == MAP_FAILED) { puts("mmap"); return 1; }
    ((char *)p)[0] = 1;
    if (msync(p, 4096, MS_SYNC) != 0) { munmap(p, 4096); puts("msync"); return 2; }
    if (mprotect(p, 4096, PROT_READ) != 0) { munmap(p, 4096); puts("mprotect"); return 3; }
    if (munmap(p, 4096) != 0) { puts("munmap"); return 4; }

    fd = open(path, O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) { puts("open"); return 5; }
    close(fd);
    ts[0].tv_sec = 1000000000LL;
    ts[0].tv_nsec = 0;
    ts[1].tv_sec = 1000000000LL;
    ts[1].tv_nsec = 0;
    if (utimensat(AT_FDCWD, path, ts, 0) != 0) { unlink(path); puts("utimensat"); return 6; }
    unlink(path);
    puts("BabyPOSIX msync/utimens: ok");
    return 0;
}
