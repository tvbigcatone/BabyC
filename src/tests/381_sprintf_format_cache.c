#include <stdio.h>
#include <string.h>
/* Anti-regression: FormatPure sprintf cache — same fmt+int args, correct output */
int main()
{
    char a[32], b[32], c[32];
    int i;
    sprintf(a, "%d-%d", 3, 7);
    sprintf(b, "%d-%d", 3, 7);   /* should hit cache */
    sprintf(c, "%d-%d", 4, 7);   /* different key */
    printf("%s\n", a);
    printf("%s\n", b);
    printf("%s\n", c);
    /* stress: many repeats */
    for (i = 0; i < 100; i++)
        sprintf(a, "n=%d", 42);
    printf("%s\n", a);
    return 0;
}
