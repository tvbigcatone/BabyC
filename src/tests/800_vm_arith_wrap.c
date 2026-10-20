/* A call-free correctness probe: a passing process exit proves the arithmetic
 * results, while the differential harness separately requires runs > 0. */
int main(void)
{
    long long max, min, a, b, c, d, e;
    max = 9223372036854775807LL;
    min = -9223372036854775807LL - 1LL;
    a = max + 1LL;
    b = min - 1LL;
    c = 3037000500LL * 3037000500LL;
    d = min / -1LL;
    e = min % -1LL;
    if (a != (-9223372036854775807LL - 1LL)) return 1;
    if (b != 9223372036854775807LL) return 2;
    if (c != -9223372036709301616LL) return 3;
    return 0;
}
