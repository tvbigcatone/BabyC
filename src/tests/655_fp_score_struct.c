#include <stdio.h>
typedef struct { int a, b, c; int (*op)(int, int); } S;
int add(int x, int y) { return x + y; }
int score(S *p) {
    int t = (p->a + p->b) + p->c;
    return p->op(t, p->a);
}
S g = {1, 2, 3, add};
int main() {
    int i, s = 0;
    for (i = 0; i < 100; i++) s += score(&g);
    printf("%d\n", s);
    return 0;
}
