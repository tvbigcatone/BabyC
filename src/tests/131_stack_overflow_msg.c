/* Must print stack overflow, not silent abort.
 * Uses large locals so the interpreter heap-stack (PICOC_STACK_SIZE /
 * STACKSIZE in picoc.c) is exhausted before:
 *   - host C stack (silent kill)
 *   - IR_CALL_MAX_DEPTH (different message)
 * Depth is unbounded: works for any STACKSIZE. */
#include <stdio.h>
void deep(void)
{
    char pad[16384];
    pad[0] = 1;
    pad[16383] = 2;
    deep();
}
int main(void)
{
    deep();
    printf("should_not_reach\n");
    return 0;
}
