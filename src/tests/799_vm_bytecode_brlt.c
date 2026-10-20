/* Production bytecode VM: BrLT and wide-immediate lowering in a call-free body. */
int main(void)
{
    int n = 7;
    int count = 0;
    while (0 < n) {
        count = count + 1;
        n = n - 1;
    }
    if (count != 7 || n != 0) return 1;
    n = -3;
    while (0 < n) {
        count = count + 1;
        n = n - 1;
    }
    if (count != 7) return 2;
    {
        long long wide = 2147483648LL;
        if (wide != 2147483648LL) return 3;
    }
    return 0;
}
