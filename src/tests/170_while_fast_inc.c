/* while with trailing i=i+1 must match for-loop sum */
#include <stdio.h>
int main()
{
    int i, n, s;
    n = 100;
    i = 0;
    s = 0;
    while (i < n)
    {
        s = s + i;
        i = i + 1;
    }
    printf("%d\n", s);
    return 0;
}
