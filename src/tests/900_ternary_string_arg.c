/* Anti-regression: ternary with string-literal arms as call args under
 * soft+strict. Copy arm must use IR_LIN_F_WIDE (non-wide Plus truncates
 * pointer to int32 → SEGV). String Imm always Tok=3 wide. */
#include <stdio.h>
int head(const char *s) { return s[0]; }
int main(void) {
    int n = 1;
    printf("%d\n", head(n ? "x" : "z"));
    n = 0;
    printf("%d\n", head(n ? "x" : "z"));
    printf("%d\n", head(n ? "ab" : ""));
    return 0;
}
