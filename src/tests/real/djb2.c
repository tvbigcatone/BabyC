/* djb2 hash — 32-bit */
#include <stdio.h>
unsigned int hash(char *s)
{
    unsigned int h = 5381;
    int i;
    i = 0;
    while (s[i] != 0)
    {
        h = h * 33 + s[i];
        i = i + 1;
    }
    return h;
}
int main()
{
    char *a;
    char *b;
    a = "hello";
    b = "BabyC";
    printf("%u\n", hash(a));
    printf("%u\n", hash(b));
    return 0;
}
