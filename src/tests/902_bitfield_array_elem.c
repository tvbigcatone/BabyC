/* Anti-regression: array of bitfield-struct under vm_strict.
 * Decl was refused (InitMap not bit-aware); load/store Tok=1 already
 * does IrBitExtract/Insert via IrSafeMemberAddr. */
#include <stdio.h>
struct B { int x:5; unsigned y:3; };
int main(void) {
    struct B a[2];
    int i;
    for (i = 0; i < 2; i++) {
        a[i].x = i + 1;
        a[i].y = (unsigned)(i + 2);
    }
    for (i = 0; i < 2; i++)
        printf("%d %d\n", a[i].x, a[i].y);
    a[0].x++;
    a[1].y++;
    printf("%d %d\n", a[0].x, a[1].y);
    return 0;
}
