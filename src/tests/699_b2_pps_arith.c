/* B.2: int **pps = ps; pps+k must scale by sizeof(void*), not sizeof(int).
 * Regression for SEGV on **(pps+1) when LocalElemSize was BaseElem (4).
 */
#include <stdio.h>
int main(void)
{
    int x = 10, y = 20, z = 30;
    int *ps[3];
    int **pps;
    ps[0] = &x; ps[1] = &y; ps[2] = &z;
    pps = ps;
    printf("%d %d %d\n", **pps, **(pps + 1), **(pps + 2));
    printf("%d\n", *(*(pps + 1)));
    return 0;
}
