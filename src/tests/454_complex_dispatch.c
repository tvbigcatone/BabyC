#include <stdio.h>

int op_add(int a, int b) { return a + b; }
int op_sub(int a, int b) { return a - b; }
int op_mul(int a, int b) { return a * b; }
int op_max(int a, int b) { return a > b ? a : b; }

int reduce(int *arr, int n, int (*op)(int, int), int init) {
    int i, acc;
    acc = init;
    for (i = 0; i < n; i = i + 1)
        acc = op(acc, arr[i]);
    return acc;
}

int chain4(int x, int (*f0)(int, int), int (*f1)(int, int),
           int (*f2)(int, int), int (*f3)(int, int), int y) {
    int r;
    r = f0(x, y);
    r = f1(r, y);
    r = f2(r, y);
    r = f3(r, y);
    return r;
}

int main() {
    int data[5];
    int (*ops[4])(int, int);
    int s;
    data[0] = 3; data[1] = 1; data[2] = 4; data[3] = 1; data[4] = 5;
    ops[0] = op_add; ops[1] = op_mul; ops[2] = op_sub; ops[3] = op_max;
    printf("%d\n", reduce(data, 5, op_add, 0));
    printf("%d\n", reduce(data, 5, op_mul, 1));
    printf("%d\n", reduce(data, 5, op_max, data[0]));
    s = chain4(10, ops[0], ops[1], ops[2], ops[3], 2);
    printf("%d\n", s);
    return 0;
}
