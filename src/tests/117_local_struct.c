/* Local struct definitions inside function / block scope.
 * Previously: "struct/union definitions can only be globals". */

#include <stdio.h>

int main()
{
    struct Point {
        int x;
        int y;
    };
    struct Point p;
    p.x = 10;
    p.y = 20;
    printf("%d %d\n", p.x, p.y);

    if (1) {
        struct Rect {
            int w;
            int h;
        };
        struct Rect r;
        r.w = 3;
        r.h = 4;
        printf("%d\n", r.w * r.h);
    }

    return 0;
}
