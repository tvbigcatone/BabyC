/* A pure non-leaf scalar callee may use its own compact stream/cache frame
 * while a terminal user call pauses the caller's compact execution. */
#include <stdio.h>
static int choose_sum(int a, int b) {
    if (a > b) return a + b;
    return a - b;
}
static int report_value(int ignored) {
    printf("%d\n", choose_sum(9, 4));
    return ignored;
}
int main(void) { return report_value(0); }
