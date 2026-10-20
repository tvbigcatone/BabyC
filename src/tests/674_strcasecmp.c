#include <stdio.h>
#include <string.h>
int main(void) {
    if (strcasecmp("AbC", "abc") != 0) return 1;
    if (strncasecmp("Hello", "HELLX", 4) != 0) return 1;
    printf("ok\n");
    return 0;
}
