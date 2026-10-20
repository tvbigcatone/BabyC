/* Anti-regression: mono shape 6 twin-rec iterative */
#include <stdio.h>
int t(int n) { if (n <= 0) return 1; return t(n - 1) + t(n - 1) + n; }
int main() { printf("%d\n", t(15)); return 0; }
