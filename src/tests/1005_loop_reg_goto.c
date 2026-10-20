#include <stdio.h>
int main(void) {
    int i = 0;
    while (i < 9) {
        i++;
        if (i == 4) goto out;
    }
out:
    printf("%d\n", i);
    return 0;
}
