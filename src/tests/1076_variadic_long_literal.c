#include <stdio.h>
#include <stdarg.h>
static long g(int n, ...) { va_list ap; long x; va_start(ap,n); x=va_arg(ap,long); va_end(ap); return x; }
int main(void) { long x=100000L; long y=21; printf("%ld\n%ld\n%ld\n",g(1,100000L),g(1,x+1),g(1,y*2L)); return 0; }
