/* typedef struct with array-of-struct member + p->arr[i].field — Body-IR */
#include <stdio.h>
typedef struct { int x; int y; } Pt;
typedef struct { Pt pts[2]; int n; } Path;
int path_sum(Path *p) {
  int i, s = 0;
  for (i = 0; i < p->n; i++)
    s = s + p->pts[i].x + p->pts[i].y;
  return s;
}
int main() {
  Path L;
  L.n = 2;
  L.pts[0].x = 1; L.pts[0].y = 2;
  L.pts[1].x = 3; L.pts[1].y = 4;
  printf("%d\n", path_sum(&L));
  return 0;
}
