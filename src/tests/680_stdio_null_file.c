/* fgets/fclose on NULL FILE* must diagnostic-fail, not silent exit. */
#include <stdio.h>
int main(void) {
    FILE *fp = 0;
    char buf[16];
    fgets(buf, 16, fp);
    return 0;
}
