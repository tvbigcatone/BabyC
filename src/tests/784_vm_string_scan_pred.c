#include <stdio.h>

int main() {
    char s[7];
    char *p;
    char *end;
    int bounded = 0;
    int nul_terminated = 0;

    s[0] = 'm'; s[1] = 'x'; s[2] = 0;
    s[3] = 'm'; s[4] = 'x'; s[5] = 'a'; s[6] = 0;
    p = s;
    end = s + 6;
    while (p < end) {
        if (*p == 'm' || *p == 'x') bounded++;
        p++;
    }

    p = s;
    while (*p) {
        if (*p == 'm' || *p == 'x') nul_terminated++;
        p++;
    }

    printf("%d %d %d\n", bounded, nul_terminated, (int)(p - s));
    return 0;
}
