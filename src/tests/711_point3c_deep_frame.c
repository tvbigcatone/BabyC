/* Point3c: recursion deeper than small pool; heap frames. */
int depth_sum(int n)
{
    if (n <= 0) return 0;
    return n + depth_sum(n - 1);
}
int main()
{
    if (depth_sum(10) != 55) return 1;
    if (depth_sum(100) != 5050) return 2;
    if (depth_sum(300) != 45150) return 3; /* > STATIC if STATIC were small; still ok */
    return 0;
}
