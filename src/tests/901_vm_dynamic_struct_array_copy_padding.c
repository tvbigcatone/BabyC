#include <stdio.h>
struct S { char tag; int value; };
int main(void) {
    struct S v[2];
    int dst = 1, src = 0;
    v[0].tag = 7;
    v[0].value = 42;
    v[1].tag = 1;
    v[1].value = 9;
    v[dst] = v[src];
    printf("%d %d %d %d\n", v[0].tag, v[0].value, v[1].tag, v[1].value);
    return 0;
}
