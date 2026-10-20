/* BabyPOSIX: readdir fills d_ino from the host entry (not always 0). */
#include <stdio.h>
#include <dirent.h>

int main(void)
{
    DIR *d = opendir(".");
    struct dirent *e;
    int n = 0;
    int nonzero = 0;

    if (d == 0) {
        printf("FAIL opendir\n");
        return 1;
    }
    while ((e = readdir(d)) != 0) {
        n++;
        if (e->d_ino != 0)
            nonzero++;
    }
    if (closedir(d) != 0) {
        printf("FAIL closedir\n");
        return 1;
    }
    if (n <= 0) {
        printf("FAIL no entries\n");
        return 1;
    }
    if (nonzero == 0) {
        printf("FAIL all d_ino were 0 (n=%d)\n", n);
        return 1;
    }
    printf("BabyPOSIX dirent ino: ok\n");
    return 0;
}
