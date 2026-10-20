#include <stdio.h>

/* Calling a function that was never declared/defined must report a
 * diagnostic ("'foo' is undefined"), not crash the interpreter (SEGV). */
int main(void)
{
    int a[4] = {1, 2, 3, 4};
    long s = 0;
    int i;
    for (i = 0; i < 4; i++)
        s += a[i];
    printf("%ld\n", s);
    foo(3);
    printf("not reached\n");
    return 0;
}
