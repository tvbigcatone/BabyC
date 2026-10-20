/* Anti-regression: mono shape 7 tail iterative */
#include <stdio.h>
int t(int n) { if (n <= 0) return 99; return t(n - 1); }
int main() { printf("%d\n", t(50)); return 0; }
