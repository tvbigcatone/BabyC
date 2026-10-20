/* BabyPOSIX unistd subset smoke test; execute with the BabyC interpreter. */
#include <unistd.h>
#include <stdio.h>

int main(void)
{
    char cwd[4096];

    if (getpid() <= 0) {
        puts("getpid failed");
        return 1;
    }
    if (getcwd(cwd, sizeof(cwd)) == 0 || cwd[0] == '\0') {
        puts("getcwd failed");
        return 2;
    }
    if (access(".", F_OK) != 0) {
        puts("access current directory failed");
        return 3;
    }
    if (access("babyposix-file-that-should-not-exist.tmp", F_OK) == 0) {
        puts("access unexpectedly found missing file");
        return 4;
    }
    if (chdir(cwd) != 0) {
        puts("chdir failed");
        return 5;
    }
    if (usleep(0) != 0) {
        puts("usleep(0) failed");
        return 6;
    }
    if (sleep(0) != 0) {
        puts("sleep(0) failed");
        return 7;
    }
    puts("BabyPOSIX unistd subset: ok");
    return 0;
}
