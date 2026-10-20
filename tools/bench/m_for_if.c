#include <stdio.h>
int main(){ int i; int x; x = 0; for (i = 0; i < 1000000; i++) if (i & 1) x = 1; printf("%d\n", x); return 0; }
