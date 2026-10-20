/* Anti-regression: shapes 2/3 still work after expansion */
#include <stdio.h>
int fib(int n) { if (n <= 1) return n; return fib(n-1)+fib(n-2); }
int fact(int n) { if (n <= 1) return 1; return n*fact(n-1); }
int main() { printf("%d %d\n", fib(20), fact(10)); return 0; }
