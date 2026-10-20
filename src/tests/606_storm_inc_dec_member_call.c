#include <stdio.h>
int neg(int x) { return -x; }
int dbl(int x) { return x + x; }
int add1(int x) { return x + 1; }
struct S { int v; int (*op)(int); int *p; };
int main() {
  struct S s; int arr[3]; int i;
  arr[0]=10; arr[1]=20; arr[2]=30;
  s.v=5; s.op=&add1; s.p=&arr[0];
  printf("a %d\n", neg(--s.v));
  printf("b %d\n", s.v);
  { struct S *ps=&s; ps->p=&arr[1];
    printf("c %d\n", dbl(++*(ps->p)));
    printf("d %d\n", arr[1]); }
  s.op=&dbl; s.v=3;
  for (i=0;i<3;i++) s.v=(*s.op)(s.v);
  printf("e %d\n", s.v);
  s.op=(s.v>10)?&neg:&add1;
  printf("f %d\n", (*s.op)(s.v));
  return 0;
}
