#include <stdio.h>
#include <stdarg.h>
static int consume(va_list ap) { int x=va_arg(ap,int); int y=va_arg(ap,int); return x+y; }
static int forward(int n, ...) { va_list ap; int r; va_start(ap,n); r=consume(ap); va_end(ap); return r; }
int main(void) { printf("%d\n",forward(2,20,22)); return 0; }
