/* Anti-regression: large automatic array (stack path) */
#include <stdio.h>
int main()
{
    int buf[1500];
    int i;
    for (i = 0; i < 1500; i++) buf[i] = i * 3;
    printf("%d %d %d\n", buf[0], buf[750], buf[1499]);
    return 0;
}
