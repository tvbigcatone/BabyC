#include <stdio.h>
int main(void) {
    printf("float=%d double=%d floatptr=%d constfloat=%d\n",
        (int)sizeof(float), (int)sizeof(double), (int)sizeof(float *), (int)sizeof(const float));
    return 0;
}
