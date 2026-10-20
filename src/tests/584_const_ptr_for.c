/* Anti-regression: const T* induction variable in for-loop → Body-IR */
#include <stdio.h>
int count_c(const char *s) {
    int n = 0;
    const char *p;
    for (p = s; *p; p++)
        n++;
    return n;
}
int main() {
    printf("%d %d\n", count_c("hi"), count_c(""));
    return 0;
}
