#include <stdio.h>
static int sum_from(int *p, int offset)
{
    return p[offset] + p[offset + 1];
}
static int char_window(const unsigned char *p, int offset)
{
    return p[offset] + p[offset + 1];
}
int main(void)
{
    int values[4];
    unsigned char bytes[4];
    values[0] = 3;
    values[1] = 7;
    values[2] = 11;
    values[3] = 13;
    bytes[0] = 2;
    bytes[1] = 4;
    bytes[2] = 6;
    bytes[3] = 8;
    printf("%d %d %d\n", sum_from(values, 0), sum_from(values, 2),
           char_window(bytes, 1));
    return 0;
}
