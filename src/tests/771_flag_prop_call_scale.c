#include <stdio.h>

/* Analysis estesa §10.1 / §10.6: *f() and f()+i scale from return pointee. */
char *ret_char_ptr(void)
{
    static char buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    return buf;
}
int *ret_int_ptr(void)
{
    static int buf[4] = {10, 20, 30, 40};
    return buf;
}
unsigned short *ret_ush_ptr(void)
{
    static unsigned short buf[4] = {100, 200, 300, 400};
    return buf;
}

struct S {
    unsigned char uc;
    unsigned short us;
};

int main(void)
{
    int sum = 0;
    struct S s;

    sum += *ret_char_ptr();
    sum += *ret_int_ptr();
    sum += *ret_ush_ptr();
    sum += ret_char_ptr()[2];
    sum += *(ret_int_ptr() + 1);
    sum += *(ret_ush_ptr() + 2);

    s.uc = 200;
    s.us = 50000;
    s.uc += 10;
    s.us >>= 1;
    sum += s.uc;
    sum += s.us;

    {
        unsigned char a[4] = {9, 8, 7, 6};
        unsigned char *p = a;
        sum += *(p + 2);
    }

    printf("%d\n", sum);
    return 0;
}
