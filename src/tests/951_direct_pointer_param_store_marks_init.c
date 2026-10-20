#include <stdio.h>
int set_first(int *p) { p[0] = 5; return 0; }
int main(void) { int a[2]; set_first(a); printf("%d\n", a[0]); return 0; }
