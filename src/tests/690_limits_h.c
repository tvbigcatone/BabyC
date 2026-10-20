#include <stdio.h>
#include <limits.h>
int main(void)
{
    printf("INT_MIN=%d INT_MAX=%d\n", INT_MIN, INT_MAX);
    printf("CHAR_MIN=%d CHAR_MAX=%d\n", CHAR_MIN, CHAR_MAX);
    printf("LLONG_MIN=%lld LLONG_MAX=%lld\n", LLONG_MIN, LLONG_MAX);
    return 0;
}
