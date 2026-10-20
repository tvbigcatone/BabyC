/* Cross kitchen-sink: rec + 3 int* + permute + switch + const char* ++ + struct read */
#include <stdio.h>

struct Box { int tag; int *p; };

int fold(const char *s, int *out)
{
    int c, n = 0;
    while ((c = *s++)) {
        n = n + c;
        *out = *out + 1;
    }
    return n;
}

void step(int k, int *a, int *b, int *c, int *score)
{
    int t;
    switch (k) {
        case 0:
            t = *a; *a = *b; *b = t;
            *score = *score + 1;
            return;
        case 1:
            t = *b; *b = *c; *c = t;
            *score = *score + 10;
            return;
        default:
            t = *c; *c = *a; *a = t;
            *score = *score + 100;
            return;
    }
}

void rec(int n, int *a, int *b, int *c, int *score)
{
    if (n <= 0)
        return;
    step(n % 3, a, b, c, score);
    rec(n - 1, b, c, a, score);
}

int main()
{
    int x = 1, y = 2, z = 3, cnt = 0, score = 0;
    struct Box bx;
    int s;
    bx.tag = 42;
    bx.p = &x;
    rec(5, &x, &y, &z, &score);
    s = fold("hi", &cnt);
    /* member read after pointer work */
    printf("%d %d %d %d %d %d %d\n", x, y, z, score, s, cnt, bx.tag);
    return 0;
}
