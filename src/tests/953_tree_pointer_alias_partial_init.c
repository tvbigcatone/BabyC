int read_second(int *p) { p[0] = 7; return p[1]; }
int main(void) { int a[2]; return read_second(a); }
