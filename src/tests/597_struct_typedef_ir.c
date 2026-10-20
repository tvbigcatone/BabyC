/* Anti-regression: typedef struct value local in Body-IR */
#include <stdio.h>
typedef struct { int x, y; } Pt;
int main() {
    Pt p;
    p.x = 3;
    p.y = 4;
    printf("%d %d\n", p.x, p.y);
    return 0;
}
