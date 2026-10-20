/* Linear STRICT regression: a void* callback parameter cast to a typed
 * pointer local is safe when null-checked and then dereferenced. */
#include <stdio.h>

static int cell = 4;
static int side_effect;

static int update(void *arg)
{
    int *p = (int *)arg;
    if (p != 0)
        *p = *p + 1;
    side_effect = 42;
    return *p;
}

int main(void)
{
    if (update((void *)&cell) != 5 || cell != 5 || side_effect != 42) {
        printf("FAIL cell=%d\n", cell);
        return 1;
    }
    printf("BabyPOSIX typed local from void pointer: ok\n");
    return 0;
}
