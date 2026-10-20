#include <stdio.h>
int total = 0;
int frame_walk(int n)
{
    int saved = n * 7 + 3;
    int child;
    total = total + n;
    if (n <= 0) return saved + total;
    child = frame_walk(n - 1);
    total = total + saved;
    return child + saved + total;
}
int main(void)
{
    int r = frame_walk(32);
    printf("%d %d\n", r, total);
    return 0;
}
