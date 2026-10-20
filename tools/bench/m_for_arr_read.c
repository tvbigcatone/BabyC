#include <stdio.h>
int main(){ int s[1000]; int i, r; long c; for (i = 0; i < 1000; i++) s[i] = 1; c = 0; for (r = 0; r < 1000; r++) for (i = 0; i < 1000; i++) c += s[i]; printf("%ld\n", c); return 0; }
