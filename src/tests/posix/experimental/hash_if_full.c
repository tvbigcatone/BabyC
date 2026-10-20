#define HIF_A 2
#define HIF_SUM(x, y) ((x) + (y))
#define HIF_TWICE(x) (HIF_SUM((x), (x)))
#if defined(HIF_A) && (HIF_A * 3 == 6) && ((1 << 3) == 8) && ((5 & 3) == 1) && ((2 ^ 3) == 1) && ((4 | 1) == 5) && ((1 | 2 & 4) == 1) && ((1 ? 9 : 7) == 9) && ((0 ? 1 : (1 ? 7 : 8)) == 7) && (HIF_UNDEFINED_NAME == 0) && ((0 && 1 || 1) == 1) && ((1 || 0 && 0) == 1) && (HIF_SUM(2, 3) == 5) && (HIF_TWICE(3) == 6) && (~0 == -1)
int main(void) { return 0; }
#else
int main(void) { return 1; }
#endif
