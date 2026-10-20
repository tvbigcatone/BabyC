/* Recursion: each frame has its own local */
#include <stdio.h>
int walk(int n)
{
    int t;
    if (n <= 0)
        return 0;
    t = n;
    return t + walk(n - 1);
}
int main()
{
    printf("%d\n", walk(5));
    return 0;
}
