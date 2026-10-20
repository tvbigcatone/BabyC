#include <stdio.h>

unsigned char values[3];

int sum_unsigned_bytes(unsigned char *p, int n)
{
    int i = 0, sum = 0;
    while (i < n) {
        sum += p[i];
        i++;
    }
    return sum;
}

void set_unsigned_bytes(unsigned char *p, int n, int value)
{
    int i = 0;
    while (i < n) {
        p[i] = value + i;
        i++;
    }
}

int main(void)
{
    values[0] = 255;
    values[1] = 128;
    values[2] = 1;
    printf("%d ", sum_unsigned_bytes(values, 3));
    set_unsigned_bytes(values, 3, 254);
    printf("%u %u %u\n", (unsigned int)values[0],
        (unsigned int)values[1], (unsigned int)values[2]);
    return 0;
}
