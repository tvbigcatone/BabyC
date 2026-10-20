/* BabyPOSIX: NULL path arguments report EFAULT. */
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>
#include <sys/time.h>

int main(void)
{
    DIR *d;

    errno = 0;
    if (access(0, F_OK) != -1 || errno != EFAULT) {
        printf("FAIL access NULL errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    if (chdir(0) != -1 || errno != EFAULT) {
        printf("FAIL chdir NULL errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    if (unlink(0) != -1 || errno != EFAULT) {
        printf("FAIL unlink NULL errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    if (rmdir(0) != -1 || errno != EFAULT) {
        printf("FAIL rmdir NULL errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    if (getcwd(0, 10) != 0 || errno != EFAULT) {
        printf("FAIL getcwd NULL errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    if (gettimeofday(0, 0) != -1 || errno != EFAULT) {
        printf("FAIL gettimeofday NULL errno=%d\n", errno);
        return 1;
    }
    errno = 0;
    d = opendir(0);
    if (d != 0 || errno != EFAULT) {
        printf("FAIL opendir NULL errno=%d\n", errno);
        return 1;
    }
    printf("BabyPOSIX unistd null: ok\n");
    return 0;
}
