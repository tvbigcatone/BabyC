/* Point4: globals resolved via CachedVal after first/compile bind. */
int g_x;
int g_y;

int add_g(void)
{
    return g_x + g_y;
}

int main()
{
    int i, s;
    g_x = 3;
    g_y = 4;
    if (add_g() != 7) return 1;
    s = 0;
    for (i = 0; i < 1000; i++)
        s = s + g_x;
    if (s != 3000) return 2;
    return 0;
}
