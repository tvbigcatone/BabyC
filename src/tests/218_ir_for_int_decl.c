#include <stdio.h>
int sumd(void)
{
    int s;
    s = 0;
    for (int i = 1; i <= 4; i = i + 1)
        s = s + i;
    return s;
}
int main()
{
    printf("%d\n", sumd());
    return 0;
}
