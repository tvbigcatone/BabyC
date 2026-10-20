/* Uninitialized local reads as 0 in IR frame (defined behaviour for this interpreter) */
#include <stdio.h>
int f(void)
{
    int x;
    return x;
}
int main()
{
    printf("%d\n", f());
    return 0;
}
