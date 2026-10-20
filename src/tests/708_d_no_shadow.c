/* Fase D: pure Body-IR with BABYC_IR_SHADOW=0 / BABYC_IR_NO_SHADOW=1.
 * Locals live only on IrCallFrame — no Variable dual-write required.
 */
#include <stdio.h>
struct S { int a; int b; };
int main(void)
{
    int x = 1, y = 2;
    int *p = &x;
    struct S s;
    int arr[3];
    s.a = 10; s.b = 20;
    arr[0] = 7; arr[1] = 8; arr[2] = 9;
    *p = 5;
    printf("%d %d %d %d %d\n", x, y, s.a, s.b, arr[1]);
    return 0;
}
