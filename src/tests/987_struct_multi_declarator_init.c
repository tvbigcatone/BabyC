#include <stdio.h>
/* One declaration, several struct declarators, any of them initialized:
 * value, pointer, array, designated and non-constant initializers. */
struct P { int x, y; };
union U { int i; char c[4]; };
double g(void) { return 2.5; }
int main(void)
{
    int k = 5;
    struct P a = {1, 2}, *p = &a, arr[2] = {{3, 4}, {5, 6}}, b, c = {.y = k * 2, .x = k};
    union U u1 = {0x41424344}, u2;
    double d = g();
    b.x = a.x + arr[1].y;
    b.y = p->y * 10;
    u2.i = u1.i + 1;
    printf("%d %d %d %d %d %d %d %d\n", a.x, a.y, p->x, arr[0].x, arr[1].y, b.x, b.y, c.x + c.y);
    printf("%d %d %.2f\n", u1.c[0], u2.c[0], d);
    {
        struct P *q = &arr[1], e = {7, 8};
        q->x += e.y;
        printf("%d %d\n", arr[1].x, e.x);
    }
    return 0;
}
