#include <stdio.h>
/* Forward prototype then definition — recursive call must use definition. */
void countdown(int);
void countdown(int n)
{
    if (n > 0)
        countdown(n - 1);
    printf("%d\n", n);
}
int main()
{
    countdown(3);
    return 0;
}
