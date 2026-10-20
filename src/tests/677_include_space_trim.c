/* Spaces inside angle brackets / quotes must still resolve built-ins. */
#include < stdio.h >
#include " string.h "
int main(void) {
    if (strcasecmp("A", "a") != 0) return 1;
    printf("ok\n");
    return 0;
}
