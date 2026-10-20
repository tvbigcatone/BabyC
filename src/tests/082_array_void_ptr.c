#include <stdio.h>
#include <string.h>

char src[8];
char dst[8];

int main()
{
    memset(src, 'X', 7);
    src[7] = '\0';
    memset(dst, 'Y', 7);
    dst[7] = '\0';
    printf("src=%s\n", src);
    printf("dst=%s\n", dst);

    memcpy(dst, src, 7);
    printf("after memcpy dst=%s\n", dst);

    memset(dst, 'Z', 3);
    printf("after memset dst=%s\n", dst);
    return 0;
}
