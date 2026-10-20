#include <stdio.h>
enum Flags { F0 = 0, F1 = 7 };
int main(void) {
    char c = -2; unsigned char uc = 3; enum Flags e = F1;
    int v = (c, e); int x = ((int)c < (unsigned int)uc);
    int y = ((unsigned char)255 > (signed char)-1);
    int z = ((0 ? c : uc) == 254);
    printf("%d %d %d %d\n", v, x, y, z);
    return 0;
}
