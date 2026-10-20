#include <stdio.h>
/* Single equality count — different literal, not m/x */
int main() {
    char s[32];
    int i, cnt = 0;
    char *q;
    for (i = 0; i < 31; i++) s[i] = (i % 3 == 0) ? 'Z' : 'y';
    s[31] = 0;
    q = s;
    while (*q) {
        if (*q == 'Z') cnt++;
        q++;
    }
    printf("%d\n", cnt);
    return 0;
}
