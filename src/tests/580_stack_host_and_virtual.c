/* Anti-regression: large Body-IR locals charge both virtual and host budgets */
#include <stdio.h>
void deep(int n) {
    char pad[2048];
    pad[0] = (char)n;
    if (n > 0) deep(n - 1);
}
int main() {
    deep(500);
    printf("should_not_reach\n");
    return 0;
}
