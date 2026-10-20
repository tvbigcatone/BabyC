#include <stdio.h>

/* Anti-regression: pure integer loops must still complete quickly (Irb/BodyIr).
 * If someone disables all IR again, this still passes functionally; timing is
 * checked manually with BABYC_IRB_TRACE=1 for "engaged". */

void main()
{
    int i, s;
    s = 0;
    for (i = 0; i < 10000; i++)
        s = s + 1;
    printf("s %d\n", s);

    i = 0;
    s = 0;
    while (i < 1000) {
        s = s + i;
        i++;
    }
    printf("w %d\n", s);
    printf("ok\n");
}
