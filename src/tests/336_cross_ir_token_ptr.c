/* Cross: Body-IR-friendly recursion calling token-path with const char* / int* */
#include <stdio.h>

int add_bytes(const char *s, int *cnt)
{
    int c;
    int n = 0;
    while ((c = *s++)) {
        n = n + c;
        *cnt = *cnt + 1;
    }
    return n;
}

/* fib-like shape for mono/IR path; mixes call to pointer function */
int mix(int n, int *cnt)
{
    if (n <= 1)
        return add_bytes(n ? "x" : "", cnt);
    return mix(n - 1, cnt) + mix(n - 2, cnt);
}

int main()
{
    int cnt = 0;
    int v = mix(4, &cnt);
    printf("%d %d\n", v, cnt);
    return 0;
}
