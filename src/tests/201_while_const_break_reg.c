/* Regression: while(1){...break;} — non-fast-path constant condition */
#include <stdio.h>
int main()
{
    int tot = 0;
    while (1) {
        tot = tot + 1;
        if (tot >= 3)
            break;
    }
    printf("%d\n", tot);
    return 0;
}
