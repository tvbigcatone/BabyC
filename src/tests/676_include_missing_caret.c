/* Expect: caret-style error for missing include (must fail). */
#include <stdio.h>
#include <no_such_header_xyzzy.h>
int main(void) { return 0; }
