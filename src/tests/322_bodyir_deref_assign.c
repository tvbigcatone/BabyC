#include <stdio.h>
/* Body IR: *p = v */
void putc_ir(char *p, int c)
{
    *p = c;
}
void main()
{
    char c;
    c = 'A';
    putc_ir(&c, 'Z');
    printf("%c\n", c);
}
