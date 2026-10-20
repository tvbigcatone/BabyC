/* sys/param.h, sys/sysmacros.h, sys/file.h, fnmatch.h compile + smoke. */
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <fnmatch.h>
#include <sched.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

int main(void)
{
    unsigned long d;
    int fd;

    if (sched_yield() != 0) { puts("sched_yield"); return 10; }
    if (MAXPATHLEN < 256) { puts("MAXPATHLEN"); return 1; }
    if (MIN(3, 5) != 3) { puts("MIN"); return 2; }
    if (MAX(3, 5) != 5) { puts("MAX"); return 3; }
    d = makedev(1, 5);
    if (major(d) != 1 || minor(d) != 5) { puts("makedev"); return 4; }
    if (fnmatch("*.c", "foo.c", 0) != 0) { puts("fnmatch yes"); return 5; }
    if (fnmatch("*.c", "foo.h", 0) == 0) { puts("fnmatch no"); return 6; }
    fd = open("bp_s7_flock.tmp", O_CREAT | O_RDWR | O_TRUNC, 0600);
    if (fd < 0) { puts("open"); return 7; }
    if (flock(fd, LOCK_EX) != 0) { puts("flock"); close(fd); return 8; }
    if (flock(fd, LOCK_UN) != 0) { puts("funlock"); close(fd); return 9; }
    close(fd);
    unlink("bp_s7_flock.tmp");
    puts("BabyPOSIX more headers: ok");
    return 0;
}
