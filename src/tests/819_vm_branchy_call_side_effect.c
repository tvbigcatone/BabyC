#include <stdio.h>

static int g = 0;

static int branchy(int x)
{
    if (x) {
        g++;
        return 9;
    }
    return 4;
}

int main(void)
{
    int result;
    result = branchy(1);
    printf("%d %d\n", result, g);
    return 0;
}
