/* Anti-regression: large-ish automatic arrays + explicit init (stack path) */
#include <stdio.h>
int main()
{
    int a[128];
    char b[256];
    int i, sum = 0;
    for (i = 0; i < 128; i++) a[i] = i;
    for (i = 0; i < 256; i++) b[i] = (char)(i & 0xff);
    for (i = 0; i < 128; i++) sum += a[i];
    for (i = 0; i < 256; i++) sum += (unsigned char)b[i];
    /* sum = 128*127/2 + 256*255/2 = 8128 + 32640 = 40768 */
    printf("%d\n", sum);
    return 0;
}
