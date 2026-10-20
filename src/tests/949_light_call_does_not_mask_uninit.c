#include <stdio.h>
int inner(void) { int z; z = 9; return z; }
int outer(void) { int x; inner(); return x; }
int main(void) { printf("%d\n", outer()); return 0; }
