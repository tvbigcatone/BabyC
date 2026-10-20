#include <stdio.h>

int st_idle(int ev);
int st_run(int ev);
int st_done(int ev);

int (*state)(int);

int st_idle(int ev) {
    if (ev == 1) {
        state = st_run;
        return 10;
    }
    return 0;
}

int st_run(int ev) {
    if (ev == 2) {
        state = st_done;
        return 20;
    }
    if (ev == 0) {
        state = st_idle;
        return 11;
    }
    return 21;
}

int st_done(int ev) {
    if (ev == 3) {
        state = st_idle;
        return 30;
    }
    return 31;
}

int main() {
    int r;
    state = st_idle;
    r = state(1);
    printf("%d\n", r);
    r = state(9);
    printf("%d\n", r);
    r = state(2);
    printf("%d\n", r);
    r = state(3);
    printf("%d\n", r);
    r = state(1);
    printf("%d\n", r);
    return 0;
}
