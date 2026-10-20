/* Anti-regression: mono shape 5 const-mult iterative */
#include <stdio.h>
int p(int n) { if (n <= 0) return 1; return 2 * p(n - 1); }
int main() { printf("%d\n", p(12)); return 0; }
