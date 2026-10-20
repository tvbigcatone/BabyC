#include <stdio.h>
int main(void)
{
    float x = 1.0f;
    float *p = &x;
    *p = 2.25f;
    printf("x=%.2f\n", x);
    *p = 3.75f;
    printf("x=%.2f\n", x);
    return 0;
}
