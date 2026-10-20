#include <stdio.h>
/* Simulated inventory: global hist, struct items, fptr score, nested walk */
struct Item {
    int sku;
    int qty;
    int (*score)(struct Item *);
};
int hist[16];
int base_score(struct Item *it) { return it->sku * 10 + it->qty; }
int bulk_score(struct Item *it) {
    int s = base_score(it);
    return it->qty > 5 ? s * 2 : s;
}
void bump_hist(int sku) {
    if (sku >= 0 && sku < 16)
        hist[sku] = hist[sku] + 1;
}
int process(struct Item *arr, int n) {
    int i, total = 0;
    for (i = 0; i < n; i = i + 1) {
        total = total + arr[i].score(&arr[i]);
        bump_hist(arr[i].sku);
    }
    return total;
}
int main() {
    struct Item a[3];
    int i, t;
    for (i = 0; i < 16; i = i + 1) hist[i] = 0;
    a[0].sku = 2; a[0].qty = 3; a[0].score = base_score;
    a[1].sku = 4; a[1].qty = 8; a[1].score = bulk_score;
    a[2].sku = 2; a[2].qty = 1; a[2].score = base_score;
    t = process(a, 3);
    printf("%d %d %d\n", t, hist[2], hist[4]);
    return 0;
}
