/* do-while with variable bound + nested in function */
#include <stdio.h>
int accum(int n)
{
    int i, s;
    i = 0; s = 0;
    do {
        s = s + i;
        i = i + 1;
    } while (i < n);
    return s;
}
int main()
{
    printf("%d %d\n", accum(5), accum(1));
    return 0;
}
