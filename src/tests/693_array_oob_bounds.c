/* Fixed-size array: index >= size must report out of bounds, not uninit. */
#include <stdio.h>
int main(void)
{
    int a[3];
    int i = 3;
    a[0] = 1; a[1] = 2; a[2] = 3;
    printf("%d\n", a[i]);
    return 0;
}
