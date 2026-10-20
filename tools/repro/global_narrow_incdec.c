/* Known semantic mismatch in BABYC_EXEC=vm / vm_strict. Keep outside src/tests
 * until the linear and compact VM match Tree; this is a reproducer, not a pass.
 */
#include <stdio.h>
signed char a = 127;
unsigned char b = 255;
short c = 32767;
unsigned short d = 65535;
int main(void)
{
    int x;
    x = a; printf("%d ", x);
    x = a++; printf("%d %d\n", x, a);
    x = ++b; printf("%d %u\n", x, b);
    x = c++; printf("%d %d\n", x, c);
    x = ++d; printf("%d %u\n", x, d);
    return 0;
}
