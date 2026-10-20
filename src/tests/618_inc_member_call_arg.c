#include <stdio.h>
struct S { int v; };
struct T { int *p; };
int neg(int x) { return -x; }
int dbl(int x) { return x + x; }
int main() {
    struct S s; struct T t; int arr[2]; int (*fp)(int);
    s.v = 5; arr[0] = 10; arr[1] = 20; t.p = &arr[0]; fp = &neg;
    printf("%d\n", neg(--s.v));
    printf("%d\n", s.v);
    printf("%d\n", dbl(++*(t.p)));
    printf("%d\n", arr[0]);
    { struct S *ps = &s; fp = &dbl;
      printf("%d\n", (*fp)(++ps->v));
      printf("%d\n", s.v); }
    return 0;
}
