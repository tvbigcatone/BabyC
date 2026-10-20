#include <stdio.h>
#include <stdarg.h>
struct Big { long long a, b, c; };
struct Small { int a; short b; };
static long long big(int n, ...) { va_list ap; struct Big b; va_start(ap,n); b=va_arg(ap,struct Big); va_end(ap); return b.a+b.b*10+b.c*100; }
static int small(int n, ...) { va_list ap; struct Small s; va_start(ap,n); s=va_arg(ap,struct Small); va_end(ap); return s.b*1000+s.a; }
int main(void) { struct Big B={1,2,3}; struct Small S={7,42}; printf("%lld\n%d\n",big(1,B),small(1,S)); return 0; }
