#include <stdio.h>
typedef int i32;
typedef unsigned long ulong;
int main() {
    int a = -1;
    i32 b = (i32)a;
    ulong c = (ulong)100;
    printf("%d %lu\n", b, c);
    return 0;
}
