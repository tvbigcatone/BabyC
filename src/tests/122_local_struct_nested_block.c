/* Local struct inside for/while and multi-declarator members. */

#include <stdio.h>

int main()
{
    int sum = 0;
    int i;
    for (i = 0; i < 3; i = i + 1) {
        struct Node {
            int val;
            int next;
        };
        struct Node n;
        n.val = i + 1;
        n.next = i * 10;
        sum = sum + n.val + n.next;
    }
    printf("%d\n", sum);

    {
        struct Pair {
            int a, b, c;
        };
        struct Pair p;
        p.a = 1;
        p.b = 2;
        p.c = 3;
        printf("%d\n", p.a + p.b + p.c);
    }
    return 0;
}
