#include <stdio.h>
int effects;
int probe(int n)
{
    effects++;
    if (n == 0) return 1;
    return (n > 0) && probe(n - 1);
}
int main(void)
{
    int r;
    effects = 0;
    r = (0 && probe(4)); printf("%d %d ", r, effects);
    r = (1 && probe(3)); printf("%d %d ", r, effects);
    r = (1 || probe(9)); printf("%d %d\n", r, effects);
    return 0;
}
