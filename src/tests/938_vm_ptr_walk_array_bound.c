#include <stdio.h>
int main(void) {
  int a[4]; int *p; int s = 0;
  a[0]=1; a[1]=2; a[2]=3; a[3]=4;
  for (p = a; p < a + 4; p = p + 1)
    s = s + *p;
  printf("%d\n", s);
  return 0;
}
