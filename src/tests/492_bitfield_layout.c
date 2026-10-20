#include <stdio.h>
struct S1 { int a:3; int b:5; int c:8; };
struct S2 { unsigned u:4; unsigned v:4; int x; };
struct S3 { int a:3; int :0; int b:3; };
struct S4 { int a:3; int b:5; };
int main() {
  printf("%d\n", (int)sizeof(struct S1));
  printf("%d\n", (int)sizeof(struct S2));
  printf("%d\n", (int)sizeof(struct S3));
  printf("%d\n", (int)sizeof(struct S4));
  return 0;
}
