#include <stdio.h>
int main()
{
    int s[8000];
    int i, j, n, c;
    int r;
    for (r = 0; r < 20; r++) {
        for (i = 0; i < 8000; i++) s[i] = 1;
        for (i = 2; i < 8000; i++)
            if (s[i])
                for (j = i + i; j < 8000; j += i) s[j] = 0;
        c = 0;
        for (i = 2; i < 8000; i++) c += s[i];
    }
    printf("%d\n", c);
    return 0;
}
