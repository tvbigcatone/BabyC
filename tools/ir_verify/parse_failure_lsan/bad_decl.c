/* Intentionally malformed: exercises partial declaration IR cleanup. */
int main(void) { int a = 1, b = 2, c = ; return 0; }
