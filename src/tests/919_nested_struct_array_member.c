/* Anti-regression: nested G.pts[i].x / path_sum(&G) under vm_strict. */
#include <stdio.h>
typedef struct { int x; int y; } Pt;
typedef struct { Pt pts[2]; int n; } Path;
Path Gpath;
int path_sum(Path *p) {
    int i, s = 0;
    for (i = 0; i < p->n; i++)
        s = s + p->pts[i].x + p->pts[i].y;
    return s;
}
int main(void) {
    Path L;
    int i, s;
    Gpath.n = 2;
    Gpath.pts[0].x = 1; Gpath.pts[0].y = 2;
    Gpath.pts[1].x = 3; Gpath.pts[1].y = 4;
    L.n = 2;
    for (i = 0; i < 2; i++) {
        L.pts[i].x = Gpath.pts[i].x * 10;
        L.pts[i].y = Gpath.pts[i].y * 10;
    }
    s = path_sum(&Gpath) + path_sum(&L);
    printf("%d %d %d\n", s, L.pts[1].x, Gpath.pts[0].y);
    return 0;
}
