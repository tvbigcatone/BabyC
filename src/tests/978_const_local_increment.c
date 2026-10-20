#include <stdio.h>
/* k++ on a const local: the fused AddLocal must not bypass the check. */
int main() {
    const int k = 5;
    int i;
    for (i = 0; i < 3; i++)
        k++;
    printf("x %d\n", k);
    return 0;
}
