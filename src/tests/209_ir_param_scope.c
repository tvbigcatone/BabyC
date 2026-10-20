#include <stdio.h>
int c(int x) {
    return x * x;
}
int b(int x) {
    return c(x) + 1;
}
int a(int x) {
    return b(x) + x;
}
int main()
{
    /* a(3) = b(3)+3 = c(3)+1+3 = 9+1+3 = 13 */
    printf("%d\n", a(3));
    /* a(4)=b(4)+4=c(4)+1+4=16+1+4=21 */
    printf("%d\n", a(4));
    return 0;
}
