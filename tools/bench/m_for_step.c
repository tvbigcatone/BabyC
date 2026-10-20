#include <stdio.h>
int main(){ int j; int x; x = 0; for (j = 0; j < 8000000; j += 8) x = 1; printf("%d\n", x); return 0; }
