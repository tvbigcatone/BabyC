/* Anti-regression: mono shape 4 sum-linear iterative */
#include <stdio.h>
int s(int n) { if (n <= 0) return 0; return s(n - 1) + n; }
int main() { printf("%d\n", s(100)); return 0; }
