#include <stdio.h>
static int v[6];
int main(void)
{
    int i = 0, sum = 0, hits = 0;
    while (i < 6) {
        i++;
        if (i == 2) continue;
        if (i == 6) break;
        if (i > 1 && i < 5) { v[i] = i * 2; hits++; }
        else v[i] = i;
    }
    i = 0;
    while (i < 6) { sum += v[i]; i++; }
    if (sum != 20 || hits != 2) return 1;
    printf("%d %d %d %d\n", sum, hits, v[3], v[5]);
    return 0;
}
