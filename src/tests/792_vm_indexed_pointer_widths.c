#include <stdio.h>
short shorts[3];
int sum_shorts(short *p) { return p[0] + p[1] + p[2]; }
void set_short(short *p, int v) { p[1] = v; }
int main(void) { shorts[0] = -300; shorts[1] = 400; shorts[2] = -1; printf("%d ", sum_shorts(shorts)); set_short(shorts, 65535); printf("%d\n", shorts[1]); return 0; }
