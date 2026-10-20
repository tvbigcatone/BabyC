#include <stdio.h>
int main()
{
    int i;
    long t;
    char buf[128];
    t = 0;
    for (i = 0; i < 200000; i++) {
        sprintf(buf, "%d:%d:%d", i, i + 1, i * 2);
        t += buf[0] + buf[1];
    }
    printf("%ld\n", t);
    return 0;
}
