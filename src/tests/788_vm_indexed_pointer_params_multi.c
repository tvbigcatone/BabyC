#include <stdio.h>

int left_values[4];
int right_values[4];

int dot_indexed(int *a, int *b, int n)
{
    int i = 0;
    int sum = 0;
    while (i < n) {
        sum += a[i] * b[i];
        i++;
    }
    return sum;
}

int main(void)
{
    int i;
    for (i = 0; i < 4; i++) {
        left_values[i] = i + 1;
        right_values[i] = (i + 1) * 10;
    }
    printf("%d ", dot_indexed(left_values, right_values, 4));
    left_values[0] = 100;
    printf("%d\n", dot_indexed(right_values, left_values, 2));
    return 0;
}
