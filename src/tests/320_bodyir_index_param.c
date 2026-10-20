#include <stdio.h>
/* Body IR: s[i] load/store on char* param */
int at(char *s, int i)
{
    return s[i];
}
void set_at(char *s, int i, int c)
{
    s[i] = c;
}
void main()
{
    char b[4];
    b[0] = 'x'; b[1] = 'y'; b[2] = 'z'; b[3] = 0;
    printf("%d %d\n", at(b, 0), at(b, 2));
    set_at(b, 1, 'Q');
    printf("%c%c%c\n", b[0], b[1], b[2]);
}
