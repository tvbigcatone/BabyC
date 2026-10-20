#include <stdio.h>
struct S { int a:4; int b:4; };
int main() {
  struct S arr[3];
  int i;
  for (i = 0; i < 3; i++) {
    arr[i].a = i;
    arr[i].b = i + 1;
  }
  for (i = 0; i < 3; i++)
    printf("%d %d\n", arr[i].a, arr[i].b);
  return 0;
}
