#include <stdio.h>
/* Anti-regression: function that only READs a global is also impure for memo
 * (result depends on mutable state, not only on args). */
int g = 1;
int f(int x)
{
    return x + g;
}
int main()
{
    int a, b, c;
    a = f(10);
    g = 100;
    b = f(10);
    c = f(10);
    printf("%d %d %d\n", a, b, c);
    return 0;
}
