/* Repro: real messages_manager stopped silently right after action_extract.
 * main (IR) -> action_extract (IR) -> find_by_id (TOKEN body, forced by the
 * #if inside the body) -> helper_a/helper_b (IR, each with a return).
 * While a token body runs IrCallDepth is 0, so the nested IR helpers reuse
 * IR frames 0 and 1 and used to overwrite the frames of main/action_extract
 * (HasReturn=1 on main's frame) -> main skipped everything after the call.
 * Must print every marker, in order, including AFTER_EXTRACT. */
#include <stdio.h>

int g_calls;

int helper_b(int n) {
    if (n < 0)
        return 0;
    return n + 1;
}

int helper_a(int n) {
    int r;
    r = helper_b(n);
    if (r > 100)
        return 100;
    return r;
}

int find_by_id(int id) {
    int r;
#if defined(NEVER_DEFINED_FEATURE)
    r = -1;
#else
    r = helper_a(id);
#endif
    g_calls = g_calls + 1;
    if (r < 0)
        return 0;
    return r;
}

int action_extract(void) {
    int i, cnt_ok;
    cnt_ok = 0;
    for (i = 0; i < 5; i++) {
        if (!find_by_id(i))
            continue;
        cnt_ok = cnt_ok + 1;
    }
    printf("  Estratte dal DB  : %d\n", cnt_ok);
    return 0;
}

int main(void) {
    int rc_exit;
    printf("BEFORE_EXTRACT\n");
    rc_exit = action_extract();
    printf("BEFORE_CLOSE_DB\n");
    printf("AFTER_EXTRACT\n");
    printf("  chiamate         : %d\n", g_calls);
    printf("  Esito            : %s\n", rc_exit == 0 ? "OK" : "ERRORE");
    return rc_exit;
}
