#include <stdio.h>
int main()
{
    int i;
    long t;
    t = 0;
    for (i = 0; i < 1000000; i++)
        t += i & 7;
    printf("%ld\n", t);
    return 0;
}
