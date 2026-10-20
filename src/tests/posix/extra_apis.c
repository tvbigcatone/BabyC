#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sched.h>
#include <getopt.h>
#include <string.h>

int main(void)
{
    char tmpl[32];
    char *dir;
    int rc;
    int longindex;
    char *argvv[4];
    struct option opts[2];

    if (sched_yield() != 0) {
        printf("FAIL sched_yield errno=%d\n", errno);
        return 1;
    }

    memcpy(tmpl, "/tmp/bpXXXXXX", 14);
    tmpl[14] = 0;
    dir = mkdtemp(tmpl);
    if (dir == 0) {
        printf("FAIL mkdtemp errno=%d\n", errno);
        return 1;
    }
    if (rmdir(dir) != 0) {
        printf("FAIL rmdir errno=%d\n", errno);
        return 1;
    }

    errno = 0;
    rc = fcntl(3, F_DUPFD, 8);
    if (rc != -1 || errno != EBADF) {
        printf("FAIL F_DUPFD badfd rc=%d errno=%d\n", rc, errno);
        return 1;
    }
    errno = 0;
    rc = fcntl(0, F_DUPFD, 300);
    if (rc != -1 || errno != EINVAL) {
        printf("FAIL F_DUPFD min 300 rc=%d errno=%d\n", rc, errno);
        return 1;
    }

    argvv[0] = "t";
    argvv[1] = "--help";
    argvv[2] = 0;
    opts[0].name = "help";
    opts[0].has_arg = no_argument;
    opts[0].flag = 0;
    opts[0].val = 'h';
    opts[1].name = 0;
    opts[1].has_arg = 0;
    opts[1].flag = 0;
    opts[1].val = 0;
    optind = 1;
    longindex = -1;
    rc = getopt_long(2, argvv, "h", opts, &longindex);
    if (rc != 'h') {
        printf("FAIL getopt_long rc=%d idx=%d\n", rc, longindex);
        return 1;
    }
    printf("extra apis ok\n");
    return 0;
}
