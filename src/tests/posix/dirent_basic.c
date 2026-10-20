#include <dirent.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    DIR *dir = opendir(".");
    struct dirent *entry;
    int found = 0;
    char first_name[256];
    if (dir == NULL) {
        printf("opendir failed\n");
        return 1;
    }
    entry = readdir(dir);
    if (entry == NULL || entry->d_name[0] == '\0') {
        printf("first readdir failed\n");
        closedir(dir);
        return 2;
    }
    {
        int i = 0;
        while (entry->d_name[i] != '\0' && i < 255) { first_name[i] = entry->d_name[i]; i++; }
        first_name[i] = '\0';
    }
    rewinddir(dir);
    entry = readdir(dir);
    if (entry == NULL || strcmp(entry->d_name, first_name) != 0) {
        printf("rewinddir failed\n");
        closedir(dir);
        return 3;
    }
    found = 0;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] != '\0') found++;
    }
    if (closedir(dir) != 0) {
        printf("closedir failed\n");
        return 4;
    }
    if (found <= 0) {
        printf("no directory entries\n");
        return 5;
    }
    printf("dirent ok: %d entries\n", found);
    return 0;
}
