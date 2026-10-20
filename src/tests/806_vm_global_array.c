/* Global-array coverage: direct scalar integer arrays use the compact VM with
 * bounds checks when enabled and transactional write logging on abort. */
static int values[8];

static int fill_and_verify(void)
{
    int i, sum;
    i = 0;
    while (i < 8)
    {
        values[i] = i * 3 + 1;
        i++;
    }
    i = 0;
    sum = 0;
    while (i < 8)
    {
        if (values[i] != i * 3 + 1) return 1;
        sum += values[i];
        i++;
    }
    return sum == 92 ? 0 : 2;
}

int main(void)
{
    return fill_and_verify();
}
