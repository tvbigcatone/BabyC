#include <stdio.h>
/* LICM must not hoist i=0 when body has i++ (nested while). */
int main(void)
{
    char buf[8];
    int i, k, n = 0;
    buf[0] = 'm';
    buf[1] = 'a';
    buf[2] = 0;
    for (k = 0; k < 3; k++) {
        i = 0;
        while (buf[i]) {
            if (buf[i] == 'm') n++;
            i++;
        }
    }
    printf("%d\n", n);
    return 0;
}
