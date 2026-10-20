#include <stdio.h>
int main(void) { int a[2]={1,2}; int *p=a; int i=0; while (i < 2) { p[i] = i+7; i++; } printf("%d %d\n", a[0], a[1]); return 0; }
