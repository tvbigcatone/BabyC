#include <stdio.h>
int main(void)
{
    unsigned char a[8]; unsigned char *q; int i;
    for (i = 0; i < 8; i++) a[i] = (unsigned char)(200 + i);
    q = a + 3;
    printf("%u %d\n", *q, *q);
    return 0;
}
