#include <stdio.h>
#include <stdarg.h>
static int sum(int n, ...) { va_list a,b,c; int x,y,z; va_start(a,n); va_copy(b,a); va_copy(c,b); x=va_arg(a,int); y=va_arg(b,int); z=va_arg(c,int); va_end(a); va_end(b); va_end(c); return x+y+z; }
int main(void) { printf("%d\n",sum(1,14)); return 0; }
