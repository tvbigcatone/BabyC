#include <stdio.h>
#include <stdint.h>

void main()
{
    char *p = "x";
    char a[] = "xy";
    int i = 0;
    int *pi = &i;
    long long ll = 0;
    long long *pll = &ll;
    int64_t i64 = 0;
    void *v = 0;

    /* --- string literals: array size including NUL (C standard) --- */
    printf("lit_x %d\n", (int)sizeof("x"));
    printf("lit_ab %d\n", (int)sizeof("ab"));

    /* --- char* variable: pointer size, NOT string length --- */
    if ((int)sizeof(p) == (int)sizeof(char *))
        printf("charptr OK\n");
    else
        printf("charptr FAIL %d %d\n", (int)sizeof(p), (int)sizeof(char *));

    /* --- char array variable --- */
    printf("arr %d\n", (int)sizeof(a));

    /* --- integers: must match type sizeof, independent of string fix --- */
    if ((int)sizeof(i) == (int)sizeof(int))
        printf("int OK\n");
    else
        printf("int FAIL %d %d\n", (int)sizeof(i), (int)sizeof(int));

    if ((int)sizeof(ll) == (int)sizeof(long long))
        printf("ll OK\n");
    else
        printf("ll FAIL %d %d\n", (int)sizeof(ll), (int)sizeof(long long));

    if ((int)sizeof(i64) == (int)sizeof(int64_t))
        printf("i64 OK\n");
    else
        printf("i64 FAIL %d %d\n", (int)sizeof(i64), (int)sizeof(int64_t));

    /* long long / int64_t should be 8 on this fork */
    if ((int)sizeof(long long) == 8 && (int)sizeof(int64_t) == 8)
        printf("64bit OK\n");
    else
        printf("64bit FAIL ll=%d i64=%d\n", (int)sizeof(long long), (int)sizeof(int64_t));

    /* --- pointers to int / long long / void: same width as char* --- */
    if ((int)sizeof(pi) == (int)sizeof(char *)
        && (int)sizeof(pll) == (int)sizeof(char *)
        && (int)sizeof(v) == (int)sizeof(char *)
        && (int)sizeof(int *) == (int)sizeof(char *)
        && (int)sizeof(long long *) == (int)sizeof(char *)
        && (int)sizeof(void *) == (int)sizeof(char *))
        printf("allptr OK\n");
    else
        printf("allptr FAIL\n");
}
