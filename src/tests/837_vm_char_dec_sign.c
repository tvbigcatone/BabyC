/* char c; c--; must sign-extend. VM light path used to yield 249 for -7. */
#include <stdio.h>
int f(void) {
    char c = -6;
    c--;
    return (int)c;
}
int g(int n) {
    char c = 0;
    int i;
    for (i = 0; i < n; i++) c--;
    return (int)c;
}
int main(void) {
    printf("%d\n", f());
    printf("%d\n", g(7));
    return 0;
}
