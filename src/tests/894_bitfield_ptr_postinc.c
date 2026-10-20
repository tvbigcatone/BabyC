/* Anti-regression: bitfield post/pre-inc through struct pointer under vm_strict.
 * Linear lower must stamp BitWidth and use IrBitExtract/Insert (not full-word). */
#include <stdio.h>
struct B { int x:5; unsigned y:3; };
int post_x(struct B *p) { return p->x++; }
int pre_y(struct B *p) { return ++p->y; }
int main(void) {
    struct B s;
    s.x = 3;
    s.y = 1;
    printf("%d\n", post_x(&s));
    printf("%d\n", s.x);
    printf("%d\n", pre_y(&s));
    printf("%d\n", s.y);
    printf("%d\n", post_x(&s));
    printf("%d\n", s.x);
    return 0;
}
