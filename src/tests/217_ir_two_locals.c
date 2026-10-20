#include <stdio.h>
int mix(int a)
{
    int b;
    int c;
    b = a + 1;
    c = b + 2;
    return a + b + c;
}
int main()
{
    /* a=4 b=5 c=7 → 16 */
    printf("%d\n", mix(4));
    return 0;
}
