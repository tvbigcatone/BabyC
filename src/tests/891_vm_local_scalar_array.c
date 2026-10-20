#include <stdio.h>
/* Compact VM: local owned scalar arrays use the canonical declaration kernel,
 * typed element access, bounds metadata and per-element transactional undo. */
int main(void)
{
    int a[3];
    a[0] = 11;
    a[1] = 22;
    a[2] = 33;
    a[1] += a[0];
    printf("%d %d %d %d\n", a[0], a[1], a[2], a[0]);
    return 0;
}
