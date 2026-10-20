#include <stdio.h>
int main(void)
{
    char input[16];
    puts("before");
    gets(input);
    puts("ERROR: gets should have been rejected");
    return 0;
}
