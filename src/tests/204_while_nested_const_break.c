/* Regression: outer while(i<n) + inner while(1){break} — parser pos after body cache */
#include <stdio.h>
int main()
{
    int tot = 0, i = 0;
    while (i < 3) {
        int j = 0;
        while (1) {
            j = j + 1;
            if (j >= 2)
                break;
        }
        tot = tot + j;
        i = i + 1;
    }
    printf("%d\n", tot);
    return 0;
}
