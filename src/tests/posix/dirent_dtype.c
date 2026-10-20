#include <dirent.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

int main(void)
{
    DIR *d;
    struct dirent *e;
    int saw_dir = 0;
    int saw_reg = 0;
    int fd;
    int n = 0;

    d = opendir(".");
    if (d == 0) {
        printf("FAIL opendir\n");
        return 1;
    }
    fd = dirfd(d);
    if (fd < 0) {
        printf("FAIL dirfd errno=%d\n", errno);
        closedir(d);
        return 1;
    }
    if (dirfd(d) != fd) {
        printf("FAIL dirfd not stable\n");
        closedir(d);
        return 1;
    }
    while ((e = readdir(d)) != 0) {
        n++;
        if (e->d_type == DT_DIR) saw_dir = 1;
        if (e->d_type == DT_REG) saw_reg = 1;
        if (e->d_name[0] == 0) {
            printf("FAIL empty name\n");
            closedir(d);
            return 1;
        }
    }
    if (n <= 0 || saw_dir == 0) {
        printf("FAIL n=%d dir=%d reg=%d\n", n, saw_dir, saw_reg);
        closedir(d);
        return 1;
    }
    if (closedir(d) != 0) {
        printf("FAIL closedir\n");
        return 1;
    }

    fd = open(".", O_RDONLY | O_DIRECTORY);
    if (fd < 0) {
        printf("FAIL open dir errno=%d\n", errno);
        return 1;
    }
    d = fdopendir(fd);
    if (d == 0) {
        printf("FAIL fdopendir errno=%d\n", errno);
        return 1;
    }
    e = readdir(d);
    if (e == 0 || e->d_name[0] == 0) {
        printf("FAIL fdopendir readdir\n");
        closedir(d);
        return 1;
    }
    if (closedir(d) != 0) {
        printf("FAIL closedir fdopendir\n");
        return 1;
    }
    printf("dirent dtype ok n=%d\n", n);
    return 0;
}
