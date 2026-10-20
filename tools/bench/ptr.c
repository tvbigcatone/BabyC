#include <stdio.h>
int main()
{
    int a[1000];
    int *p;
    int i, r;
    long t;
    for (i = 0; i < 1000; i++) a[i] = i;
    t = 0;
    for (r = 0; r < 300; r++) {
        p = a;
        for (i = 0; i < 1000; i++)
            t += p[i];
    }
    printf("%ld\n", t);
    return 0;
}
