/* array_sum dense path: for (i=0;i<N;i++) s += a[i]; must hit FastArrayAccum. */
int a[256];
int main()
{
    int i, s, N;
    N = 256;
    for (i = 0; i < N; i++)
        a[i] = i;
    s = 0;
    for (i = 0; i < N; i++)
        s = s + a[i];
    if (s != 32640) return 1; /* 255*256/2 */
    s = 0;
    for (i = 0; i < N; i++)
        s += a[i];
    if (s != 32640) return 2;
    return 0;
}
