/* Anti-regression: Body-IR local array init + return + printf.
 * Stack-charging must not corrupt subsequent Intrinsic calls. */
#include <stdio.h>
int sum3(void) {
    int v[3] = { 10, 20, 30 };
    int i, s = 0;
    for (i = 0; i < 3; i = i + 1)
        s = s + v[i];
    return s;
}
int main(void) {
    printf("%d\n", sum3());
    return 0;
}
