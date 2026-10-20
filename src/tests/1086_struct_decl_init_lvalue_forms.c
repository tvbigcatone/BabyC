#include <stdio.h>
struct P { int x, y; };
struct W { struct P in; long tag; };
struct B { double d; long long k; char name[12]; };
union U { int i; short s[2]; };
struct P gp = {5, 6};
struct P garr[3] = {{1, 1}, {2, 2}, {3, 3}};
struct B gb[2] = {{1.5, 10, "first"}, {2.5, 20, "second"}};
static struct P mk(int a) { struct P r; r.x = a; r.y = a * 2; return r; }
static void scalars_and_nested(void) {
    struct P arr[3] = {{1, 2}, {3, 4}, {5, 6}};
    struct W w; struct P *pp = &arr[1]; struct W *pw = &w;
    int i = 0;
    w.in.x = 8; w.in.y = 7; w.tag = 3;
    {
        struct P q1 = arr[i++];
        struct P q2 = w.in;
        struct P q3 = *pp;
        struct P q4 = gp;
        struct P q5 = garr[i++];
        struct P q6 = pw->in;
        struct P q7 = pp[1];
        printf("%d %d %d\n", q1.x, q1.y, i);
        printf("%d %d %d %d %d %d\n", q2.x, q2.y, q3.x, q3.y, q4.x, q4.y);
        printf("%d %d %d %d %d %d\n", q5.x, q5.y, q6.x, q6.y, q7.x, q7.y);
        /* copies are values, not aliases of their sources */
        arr[0].x = 100; w.in.x = 101; pp->x = 102; gp.x = 103; garr[1].x = 104;
        printf("%d %d %d %d %d %d %d\n", q1.x, q2.x, q3.x, q4.x, q5.x, q6.x, q7.x);
    }
}
static void unions_wide_calls(void) {
    union U u, *pu = &u; struct B *pb = &gb[1];
    int i = 2;
    u.i = 65538;
    {
        union U v = *pu;
        struct B b1 = *pb;
        struct B b2 = gb[i - 2];
        struct P m1 = mk(1);
        struct P m2 = mk(2);
        printf("%d %.1f %lld %s %.1f %lld %s\n", v.i, b1.d, b1.k, b1.name, b2.d, b2.k, b2.name);
        printf("%d %d %d %d\n", m1.x, m1.y, m2.x, m2.y);
        u.i = 0; pb->k = 0; gb[0].name[0] = 'X';
        printf("%d %lld %s\n", v.i, b1.k, b2.name);
    }
}
int main(void) {
    scalars_and_nested();
    unions_wide_calls();
    return 0;
}
