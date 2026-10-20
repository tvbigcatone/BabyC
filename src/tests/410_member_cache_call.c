/* Cross: struct member via param pointer + call in loop */
#include <stdio.h>
typedef struct { int x, y; int data[4]; } Node;
int score(Node *n) {
    int i, s;
    s = n->x + n->y;
    for (i = 0; i < 4; i = i + 1)
        s = s + n->data[i];
    return s;
}
int main() {
    Node pool[4];
    int i, j, t = 0;
    for (i = 0; i < 4; i = i + 1) {
        pool[i].x = i;
        pool[i].y = i * 2;
        for (j = 0; j < 4; j = j + 1)
            pool[i].data[j] = i + j;
    }
    for (i = 0; i < 100; i = i + 1)
        t = t + score(&pool[i % 4]);
    printf("%d\n", t);
    return 0;
}
