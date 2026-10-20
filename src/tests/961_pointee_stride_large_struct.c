struct LargeStrideItem {
    char bytes[300];
    int marker;
};

int main(void)
{
    struct LargeStrideItem items[2];
    struct LargeStrideItem *p = items;
    items[0].marker = 0;
    items[1].marker = 0;
    p[0].marker = 17;
    p[1].marker = 29;
    if (items[0].marker != 17 || items[1].marker != 29)
        return 1;
    return 0;
}
