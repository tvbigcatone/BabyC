#include <stdio.h>
int data[8];
int calls = 0;
int *base = data;
int store_and_descend(int n)
{
    calls = calls + 1;
    *(base + n) = n + calls * 100;
    if (n == 0) return *(base + n);
    return store_and_descend(n - 1) + *(base + n);
}
int main(void)
{
    int r = store_and_descend(4);
    printf("%d %d %d %d %d %d\n", r, calls, data[0], data[1], data[2], data[4]);
    return 0;
}
