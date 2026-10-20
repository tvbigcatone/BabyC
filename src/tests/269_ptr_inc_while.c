#include <stdio.h>

/* Anti-regression: while (*p) p++ and p-- on char* must use pointer arithmetic
 * in Loop Body IR (not integer +1 via ExpressionAssignInt). */

void main()
{
    char s[16];
    char *p;
    int n;

    s[0] = 'x'; s[1] = 'y'; s[2] = 'z'; s[3] = 0;

    /* walk to NUL */
    n = 0;
    p = s;
    while (*p != 0) {
        n = n + 1;
        p++;
    }
    printf("len %d\n", n);

    /* walk with sentinel ';' */
    s[0] = 'a'; s[1] = ';'; s[2] = 'b'; s[3] = 0;
    n = 0;
    p = s;
    while (*p != 0 && *p != ';') {
        n = n + 1;
        p++;
    }
    printf("to_semi %d ch %c\n", n, *p);

    /* postfix then continue */
    if (*p == ';')
        p++;
    n = 0;
    while (*p != 0) {
        n = n + 1;
        p++;
    }
    printf("after %d\n", n);

    /* decrement back */
    p = s + 3; /* points at NUL of "a;b" */
    p--;
    printf("back %c\n", *p);

    printf("ok\n");
}
