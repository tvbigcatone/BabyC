/* BabyPOSIX times/getrusage/pathconf/getpgrp. */
#include <sys/times.h>
#include <sys/resource.h>
#include <unistd.h>
#include <stdio.h>
#include <strings.h>

int main(void)
{
    struct tms t;
    struct rusage ru;
    long c;
    char *p;

    c = times(&t);
    if (c < 0) { puts("times"); return 1; }
    if (getrusage(RUSAGE_SELF, &ru) != 0) { puts("getrusage"); return 2; }
    if (getpgrp() <= 0) { puts("getpgrp"); return 3; }
    if (pathconf(".", _PC_PATH_MAX) < 256) { puts("pathconf"); return 4; }
    if (getdtablesize() < 8) { puts("getdtablesize"); return 5; }
    p = index("hello", 'l');
    if (p == 0 || p[0] != 'l') { puts("index"); return 6; }
    p = rindex("hello", 'l');
    if (p == 0 || p[1] != 'o') { puts("rindex"); return 7; }
    if (sysconf(_SC_CLK_TCK) < 1) { puts("sysconf clk"); return 8; }
    puts("BabyPOSIX times/rusage: ok");
    return 0;
}
