/* Global array-of-struct assignment must copy aggregate bytes, not a pointer. */
#include <stdio.h>
typedef struct { int a; int b; int c; } S;
S table[4];
int main(void) {
    int i = 0, j = 2;
    table[0].a = 10; table[0].b = 20; table[0].c = 30;
    table[2].a = 40; table[2].b = 50; table[2].c = 60;
    table[i] = table[j];
    table[1] = table[i];
    table[i] = table[i];
    table[1].b += 7;
    printf("%d %d %d %d %d %d\n", table[0].a, table[0].b, table[0].c,
           table[1].a, table[1].b, table[1].c);
    return 0;
}
