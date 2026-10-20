/* pointer + int, - int, element diff, comparisons */
#include <stdio.h>
int main()
{
    int a[5];
    int *p;
    int *q;
    int i;
    for (i = 0; i < 5; i = i + 1)
        a[i] = (i + 1) * 10;
    p = a;
    q = a + 4;
    printf("%d\n", *p);
    printf("%d\n", *(p + 2));
    printf("%d\n", *(q - 1));
    printf("%d\n", (int)(q - p));
    printf("%d\n", p < q);
    printf("%d\n", p == a);
    p = p + 1;
    printf("%d\n", *p);
    return 0;
}
