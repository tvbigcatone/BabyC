#include <stdio.h>
int tab[8192];
/* fill with expression tree ArrayFill does NOT accept → tree linear */
int fill(int n) {
    int i;
    for (i = 0; i < n; i = i + 1)
        tab[i] = i * 3 + 1;
    return n;
}
int sum(int n) {
    int i, s = 0;
    for (i = 0; i < n; i = i + 1)
        s = s + tab[i];
    return s;
}
int run(int reps) {
    int r, t = 0;
    for (r = 0; r < reps; r = r + 1) {
        fill(2048);
        t = t + sum(2048);
    }
    return t;
}
int main() {
    printf("%d\n", run(800));
    return 0;
}
