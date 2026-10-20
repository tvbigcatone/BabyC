#include <stdio.h>
#include <string.h>
/* ISOLATED: snprintf FormatPure cache + truncation safety */
int main()
{
    char a[16], b[16], c[4];
    int n;
    snprintf(a, 16, "%d-%d", 3, 7);
    snprintf(b, 16, "%d-%d", 3, 7); /* hit */
    snprintf(c, 4, "%d-%d", 3, 7);   /* may truncate — still correct */
    n = snprintf(a, 16, "%d", 99);
    printf("%s\n", b);
    printf("%d\n", n);
    printf("%s\n", a);
    printf("%d\n", (int)strlen(c));
    return 0;
}
