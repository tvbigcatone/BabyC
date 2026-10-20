#include <stdio.h>

static int nested_char(char **text)
{
    return text[0][1];
}

int main(void)
{
    char *text = "ab";
    char **ptr = &text;
    printf("%d\n", nested_char(ptr));
    return 0;
}
