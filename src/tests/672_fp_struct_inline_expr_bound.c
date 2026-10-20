/* for with expression bound k < 1+(i%3) + thin call body */
#include <stdio.h>
static int g;
static int bump(int x) { g = g + x; return 1; }
int main(void) {
    int i, k;
    g = 0;
    for (i = 0; i < 6; i++) {
        for (k = 0; k < 1 + (i % 3); k++)
            if (!bump(i + k)) return 1;
    }
    /* i=0: k<1 -> 0; i=1: k<2 -> 1+2; i=2: k<3 -> 2+3+4; i=3: k<1 -> 3;
       i=4: k<2 -> 4+5; i=5: k<3 -> 5+6+7
       sum = 0 + 3 + 9 + 3 + 9 + 18 = 42 */
    printf("%d\n", g);
    return 0;
}
