#include <stdio.h>
typedef int *ip;
int main() {
    int x = 7;
    ip p = &x;
    *p = 9;
    printf("%d\n", x);
    return 0;
}
