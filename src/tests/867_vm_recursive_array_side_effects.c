#include <stdio.h>
int values[8];
int write_rec(int n)
{
    if (n <= 0) {
        values[0] = values[0] + 1;
        return values[0];
    }
    values[n] = n * 10;
    return write_rec(n - 1) + values[n];
}
int main(void)
{
    int r = write_rec(4);
    printf("%d %d %d %d %d %d\n", r, values[0], values[1], values[2], values[3], values[4]);
    return 0;
}
