#include <stdio.h>
int main()
{
    int a[8000];
    int i, r;
    long t;
    for (i = 0; i < 8000; i++) a[i] = i;
    t = 0;
    for (r = 0; r < 200; r++)
        for (i = 0; i < 8000; i++)
            t += a[i];
    printf("%ld\n", t);
    return 0;
}
