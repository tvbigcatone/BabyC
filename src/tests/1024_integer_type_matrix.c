#include <stdio.h>
enum E { E_NEG = -3, E_POS = 5 };
int main(void) {
    signed char sc = -7; unsigned char uc = 250;
    short sh = -9; unsigned short ush = 60000;
    enum E e = E_NEG; char c = (char)-2;
    printf("%d %d %d %d\n", sc + 10, uc + 10, sh + 10, ush > 50000);
    printf("%d %d %d %d\n", e < 0, e + 8, c < 0, (unsigned char)uc > (unsigned int)200);
    printf("%d %d %d\n", (int)(sc < uc), (int)(sh < ush), (int)((e + 2) == -1));
    return 0;
}
