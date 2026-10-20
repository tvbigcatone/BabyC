#include <stdio.h>
typedef struct { int a, b, c; } S;
int score(S *p) { return p->a + p->b + p->c; }
S global;
int main() {
    int i;
    long long t = 0;
    global.a = 3; global.b = 5; global.c = 7;
    for (i = 0; i < 500000; i++)
        t = t + score(&global);
    printf("%lld\n", t);
    return 0;
}
