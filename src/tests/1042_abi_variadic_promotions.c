#include <stdio.h>
#include <stdarg.h>
static double values(int n, ...) { va_list ap; int i; double s=0.0; va_start(ap,n); for(i=0;i<n;i++) { if(i<2) s += va_arg(ap,int); else s += va_arg(ap,double); } va_end(ap); return s; }
int main(void) { char c=-3; short s=12; float f=2.5f; printf("%.1f\n",values(3,c,s,f)); return 0; }
