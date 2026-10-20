#include <stdio.h>
#include <string.h>
/* Pointer arithmetic as host-call argument: printf("%s", text + 2) was packed
 * as int in the tree (format mismatch diagnostic) and refused by linear;
 * (text + 5) - 2 was a narrow int subtraction in linear (truncated address). */
int main(void)
{
    char text[16] = "abcdef";
    char out[16];
    char *q = text + 1;
    int i;
    strcpy(out, text + 1);
    puts(out);
    printf("%s|%s|%s\n", text + 2, q + 1, (text + 5) - 2);
    strncpy(out, q + 2, 2);
    out[2] = 0;
    printf("%s %d %d\n", out, (int)strlen(text + 3), (int)((q + 3) - text));
    for (i = 0; i < 3; i++)
        printf("[%s]", text + i * 2);
    printf("\n");
    memset(out + 1, 'z', 3);
    out[4] = 0;
    printf("%s %d\n", out, memcmp(text + 1, "bcd", 3));
    {
        /* nested pointer arithmetic keeps pointer width and element scale */
        int a[6] = {1, 2, 3, 4, 5, 6};
        short s[5] = {9, 8, 7, 6, 5};
        int *e = (a + 5) - 2;
        char *r = (text + 5) - 2;
        printf("%d %d %d %s %s\n", *e, *((a + 1) + 3), *((s + 4) - 3), r, 1 + (text + 1));
    }
    return 0;
}
