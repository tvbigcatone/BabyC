#include <stdio.h>
#include <string.h>

int main(void)
{
    char text[8] = "abc";
    puts("before");
    strcpy(text + 1, text);
    puts("ERROR: overlapping strcpy was not diagnosed");
    return 0;
}
