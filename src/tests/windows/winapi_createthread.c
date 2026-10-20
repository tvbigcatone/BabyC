/* libWINAPI CreateThread with interpreted start routine (shared GIL trampoline). */
#include <stdio.h>
#include <windows.h>

static int g_flag = 0;

DWORD WINAPI worker(LPVOID arg)
{
    int *p = (int *)arg;
    if (p != 0)
        *p = *p + 1;
    g_flag = 99;
    return 0;
}

int main(void)
{
    HANDLE h;
    DWORD tid = 0;
    int cell = 10;
    void *start = worker;
    DWORD wait;

    h = CreateThread(0, 0, start, &cell, 0, &tid);
    if (h == 0) {
        printf("FAIL CreateThread null handle err=%u\n", (unsigned)GetLastError());
        return 1;
    }
    wait = WaitForSingleObject(h, 5000);
    if (wait != 0) {
        printf("FAIL wait=%u\n", (unsigned)wait);
        return 1;
    }
    if (g_flag != 99) {
        printf("FAIL g_flag=%d\n", g_flag);
        return 1;
    }
    if (cell != 11) {
        printf("FAIL cell=%d\n", cell);
        return 1;
    }
    printf("libWINAPI CreateThread: ok\n");
    return 0;
}
