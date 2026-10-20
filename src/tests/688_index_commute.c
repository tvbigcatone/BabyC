/* C: a[i] == *(a+i) == i[a]. Support integer[array] form. */
#include <stdio.h>
int main(void)
{
    int a[] = {11, 22, 33, 44, 55};
    printf("%d %d %d\n", a[4], *(a + 4), 4[a]);
    return 0;
}
