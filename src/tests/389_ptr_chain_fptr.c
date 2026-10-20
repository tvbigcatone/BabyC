#include <stdio.h>
struct H {
    int (*fp)(int);
    struct H *next;
};
int inc(int x) { return x + 1; }
int dec(int x) { return x - 1; }
int run(struct H *h, int x)
{
    while (h) {
        x = h->fp(x);
        h = h->next;
    }
    return x;
}
int main()
{
    struct H a, b;
    a.fp = inc; a.next = &b;
    b.fp = dec; b.next = 0;
    printf("%d\n", run(&a, 10));
    a.fp = dec; b.fp = inc;
    printf("%d\n", run(&a, 10));
    return 0;
}
