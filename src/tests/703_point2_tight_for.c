/* Point2: for (i=0;i<N;i++) s=s+i → IR_FP_TIGHT_FOR at compile. */
int main()
{
    int i, s;
    s = 0;
    for (i = 0; i < 10; i++)
        s = s + i;
    if (s != 45) return 1;
    s = 0;
    for (i = 1; i <= 100; i++)
        s = s + i;
    if (s != 5050) return 2;
    return 0;
}
