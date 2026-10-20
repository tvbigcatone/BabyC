#include <stdio.h>
int main(void)
{
    unsigned char a[8]; unsigned char *q; int i;
    for (i = 0; i < 8; i++) a[i] = (unsigned char)(200 + i);
    q = a + 3;
    printf("%u %u\n", (unsigned)q[2], (unsigned)q[0]);
    return 0;
}
