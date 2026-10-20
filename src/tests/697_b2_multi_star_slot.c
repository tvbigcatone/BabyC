/* B.2: multi-level pointer locals — DeclPointeeSize / LocalTyp on slot.
 * Run under BABYC_IR_SHADOW=0 for pure frame path.
 */
#include <stdio.h>
typedef int *ip;
int main(void)
{
    int x = 10, y = 20;
    int *p = &x;
    int **pp = &p;
    int ***ppp = &pp;
    int *arr[2];
    ip q = &y;
    ip *pp2 = &q;

    **pp = 11;
    printf("%d %d\n", x, ***ppp);

    arr[0] = &x;
    arr[1] = &y;
    printf("%d %d\n", *arr[0], *arr[1]);

    *pp2 = &x;
    printf("%d\n", *q);
    return 0;
}
