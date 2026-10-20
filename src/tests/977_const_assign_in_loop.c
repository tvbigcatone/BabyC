#include <stdio.h>
/* Assignment to a const local inside a counted loop: the loop kernel
 * fast path must not bypass the read-only diagnostic. */
int main() {
    const int k = 5;
    int i;
    for (i = 0; i < 3; i++)
        k = i;
    printf("x %d\n", k);
    return 0;
}
