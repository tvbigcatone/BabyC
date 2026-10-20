/* Expect runtime fail - not a normal pass test; used manually.
 * Suite harness skips if expect is special - we use a pass path:
 * verify legal access only; oob covered by unit check in HANDOFF.
 */
#include <stdio.h>
int main() {
    int a[2];
    a[0] = 10; a[1] = 20;
    printf("%d %d\n", a[0], a[1]);
    return 0;
}
