#include <stdio.h>

struct Point {
    int x;
    int y;
};

void main()
{
    struct Point pts[3] = {
        {1, 2},
        {3, 4},
        {5, 6}
    };
    struct Point unsized[] = {
        {10, 20},
        {30, 40}
    };
    int i;

    for (i = 0; i < 3; i++)
        printf("%d %d\n", pts[i].x, pts[i].y);

    printf("%d %d\n", unsized[0].x, unsized[0].y);
    printf("%d %d\n", unsized[1].x, unsized[1].y);
}
