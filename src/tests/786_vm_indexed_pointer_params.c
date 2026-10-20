#include <stdio.h>

int values[4];

int sum_indexed(int *p, int n)
{
    int i = 0;
    int total = 0;
    while (i < n) {
        total += p[i];
        i++;
    }
    return total;
}

void store_indexed(int *p, int n, int value)
{
    int i = 0;
    while (i < n) {
        p[i] = value + i;
        i++;
    }
}

int main(void)
{
    values[0] = 3;
    values[1] = 5;
    values[2] = 7;
    values[3] = 11;
    printf("%d ", sum_indexed(values, 4));
    store_indexed(values, 4, 10);
    printf("%d %d %d %d\n", values[0], values[1], values[2], values[3]);
    return 0;
}
