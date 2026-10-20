/* Repro: Body-IR main → Body-IR mid → token fn → Body-IR helper(return).
 *
 * When IR calls a token-path function, IrCallDepth is zeroed so the token
 * body does not see IR locals. Nested Body-IR then reuses frames 0.. and
 * overwrites outer frames (including main). If only the immediate outer
 * HasReturn is saved, main keeps HasReturn=1 after mid returns and skips
 * everything after the call (silent exit, same as messages_manager
 * extractFromDB after action_extract).
 *
 * Local typedef forces token-path compile of token_fn (Body-IR rejects
 * "typedef-name declaration"). helper_ir stays Body-IR and sets HasReturn
 * on the frame index it reuses (0 = main when depth was zeroed).
 */
#include <stdio.h>

static int helper_ir(int n)
{
    return n + 1;
}

static int token_fn(int id)
{
    typedef int MyInt;
    MyInt x = id;
    return helper_ir(x);
}

static int action_extract(void)
{
    int i;
    int ok = 0;
    for (i = 1; i <= 3; i++) {
        if (!token_fn(i))
            return 1;
        ok++;
    }
    printf("EXTRACT_DONE %d\n", ok);
    return 0;
}

int main(void)
{
    int rc;
    printf("BEFORE_EXTRACT\n");
    rc = action_extract();
    printf("AFTER_EXTRACT\n");
    printf("rc=%d\n", rc);
    return rc;
}
