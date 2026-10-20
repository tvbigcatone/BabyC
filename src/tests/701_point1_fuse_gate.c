/* Point1: patterns that must remain FUSABLE (isort-like loop body). */
int main()
{
    int a[8];
    int i, j, key;
    for (i = 0; i < 8; i++)
        a[i] = 8 - i;
    for (i = 1; i < 8; i++) {
        key = a[i];
        j = i - 1;
        while (j >= 0 && a[j] > key) {
            a[j + 1] = a[j];
            j = j - 1;
        }
        a[j + 1] = key;
    }
    /* sorted 1..8 */
    if (a[0] != 1) return 1;
    if (a[7] != 8) return 2;
    if (a[3] != 4) return 3;
    return 0;
}
