/* Regression: an assignment diagnostic in a nested call must not leak locals. */
static int fail_in_callee(void)
{
    char src_bad[3];
    int dst_bad[] = src_bad;
    return 0;
}

int main(void)
{
    return fail_in_callee();
}
