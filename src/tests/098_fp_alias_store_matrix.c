#include <stdio.h>

static void write_alias(float *p, float **pp, int depth)
{
    if (depth == 0) {
        *p = 1.25f;
        **pp = 2.5f;
        return;
    }
    write_alias(p, pp, depth - 1);
    *p = *p + 0.5f;
}

static void write_array(float *p)
{
    p[0] = 3.25f;
    *(p + 1) = 4.5f;
}

int main(void)
{
    float x = 0.0f;
    float *p = &x;
    float *q = p;
    float values[2] = { 0.0f, 0.0f };
    write_alias(p, &q, 2);
    write_array(values);
    printf("alias=%.2f values=%.2f,%.2f\n", x, values[0], values[1]);
    return 0;
}
