#include <stdio.h>
double f(int n){ return n; }
double g(int n){ if(n<2) return n; return 2.5; }
float  h(int n){ return n + 1; }
double k(long n){ return n * 2; }
int main(){ printf("%f %f %f %f %f\n", f(3), g(1), g(5), h(2), k(4)); return 0; }
