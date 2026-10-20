#include <stdio.h>
int main(void)
{
    int i = 0;
    int sum = 0;
    while (i < 200000) {
        sum += i & 7;
        i++;
    }
    printf("%d %d\n", i, sum);
    return 0;
}
