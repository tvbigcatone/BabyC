#include <stdio.h>
/* F5: char[] / short[] Index load on local arrays. */
int main(void)
{
    char c[6];
    short s[5];
    int i, sum;
    for (i = 0; i < 6; i++)
        c[i] = (char)(i * 2 + 3);
    for (i = 0; i < 5; i++)
        s[i] = (short)(i * 10 - 2);
    sum = c[0] + c[2] + c[5];
    printf("%d\n", sum);
    printf("%d\n", s[1] + s[4]);
    return 0;
}
