/* nested calls + pointer mutate */
#include <stdio.h>
void bump(int *p) { *p = *p + 1; }
int add(int a, int b) { return a + b; }
int main() {
    int x;
    x = 10;
    bump(&x);
    bump(&x);
    printf("%d\n", add(x, 5));
    return 0;
}
