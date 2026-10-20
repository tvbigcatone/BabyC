#include <stdio.h>
static int sink;
int add1(int x) { sink ^= x; return x + 1; }
int main() {
    int i, s = 0;
    for (i = 0; i < 2000000; i = i + 1)
        s = add1(s);
    printf("%d %d\n", s, sink);
    return 0;
}
