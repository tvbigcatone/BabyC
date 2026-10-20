#include <stdio.h>
/* Anti-regression: pure int functions remain memo-eligible */
int sq(int x) { return x * x; }
int main()
{
    printf("%d %d %d\n", sq(3), sq(3), sq(4));
    return 0;
}
