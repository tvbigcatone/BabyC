/* float==double storage: scanf %f must write double-sized value.
 * Also &f must pack as pointer in Body-IR (not FP bits).
 * Uses sscanf (not scanf) so the test suite needs no stdin. */
#include <stdio.h>
int main(void)
{
    float f;
    double d;
    int i;
    if (sscanf("1.5 2.5 3", "%f %lf %d", &f, &d, &i) != 3)
        return 1;
    printf("f=%.2f d=%.2f i=%d\n", f, d, i);
    return 0;
}
