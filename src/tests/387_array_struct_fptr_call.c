#include <stdio.h>
struct Node {
    int v;
    int (*fold)(int, int);
};
int add(int a, int b) { return a + b; }
int sub(int a, int b) { return a - b; }
int walk(struct Node *a, int n, int acc)
{
    int i;
    for (i = 0; i < n; i++)
        acc = a[i].fold(acc, a[i].v);
    return acc;
}
int main()
{
    struct Node a[3];
    a[0].v = 10; a[0].fold = add;
    a[1].v = 3;  a[1].fold = sub;
    a[2].v = 4;  a[2].fold = add;
    printf("%d\n", walk(a, 3, 0));
    return 0;
}
