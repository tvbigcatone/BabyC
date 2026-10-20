/* Cross: dense dual index s += a[i] + a[i&7] */
#include <stdio.h>
int main() {
    int a[16];
    int i, n, s = 0;
    for (i = 0; i < 16; i = i + 1) a[i] = i + 1;
    for (n = 0; n < 1000; n = n + 1)
        for (i = 0; i < 16; i = i + 1)
            s = s + a[i] + a[i & 7];
    printf("%d\n", s);
    return 0;
}
