#include <stdio.h>
int g=0; static void bump(void) { g++; }
int main(void) { int n=0; while (g < 3) { bump(); n++; } printf("%d %d\n", g, n); return 0; }
