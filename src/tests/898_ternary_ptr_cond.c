/* Anti-regression: ternary with pointer condition must not inherit IR_TF_PTR
 * from the condition (p ? *p : 99 is INT). Strict must lower and run. */
#include <stdio.h>
int main(void) {
    int x = 7;
    int *p = &x;
    int *n = 0;
    printf("%d\n", p ? *p : 99);
    printf("%d\n", n ? *n : 99);
    printf("%d\n", p ? 1 : 0);
    return 0;
}
