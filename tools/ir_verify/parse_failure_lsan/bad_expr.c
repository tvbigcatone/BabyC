/* Intentionally malformed: exercises partial expression IR cleanup. */
int main(void) { int a = (1 + ; return 0; }
