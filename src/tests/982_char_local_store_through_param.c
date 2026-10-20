#include <stdio.h>
/* *b = t with t a char local in a callee: store width comes from the
 * declared pointee (char), not from slot heuristics of another frame. */
void sw2(char *a, char *b)
{
    char t;
    while (a < b) { t = *a; *a = *b; *b = t; a++; b--; }
}
void put16(short *p, short v) { short w; w = v; *p = w; }
int main()
{
    char l2[8] = "abcde";
    short sh[3] = {1, 2, 3};
    long long big[2] = {0, 0};
    sw2(l2, l2 + 4);
    printf("[%s]\n", l2);
    put16(&sh[1], -7);
    printf("%d %d %d\n", sh[0], sh[1], sh[2]);
    {
        long long *q = big; long long x = 1234567890123LL;
        *q = x;
        printf("%lld %lld\n", big[0], big[1]);
    }
    return 0;
}
