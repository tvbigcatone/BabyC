/* Anti-regression: null p->a[0] under vm_strict. */
typedef struct { char a[4]; } S;
int geta(S *p) {
    return p->a[0];
}
void main(void) {
    geta(0);
}
