/* GetExitCodeThread + CRITICAL_SECTION (libWINAPI parity with BabyPOSIX sync). */
#include <stdio.h>
#include <windows.h>

static int g_shared = 0;
static CRITICAL_SECTION g_cs;

DWORD WINAPI worker(LPVOID arg)
{
    int i;
    (void)arg;
    for (i = 0; i < 100; i++) {
        EnterCriticalSection(&g_cs);
        g_shared = g_shared + 1;
        LeaveCriticalSection(&g_cs);
    }
    return 7;
}

int main(void)
{
    HANDLE h;
    DWORD tid = 0;
    DWORD code = 0;
    void *start = worker;
    BOOL ok;

    InitializeCriticalSection(&g_cs);
    h = CreateThread(0, 0, start, 0, 0, &tid);
    if (h == 0) {
        printf("FAIL create\n");
        return 1;
    }
    if (WaitForSingleObject(h, 5000) != 0) {
        printf("FAIL wait\n");
        return 1;
    }
    ok = GetExitCodeThread(h, &code);
    if (!ok) {
        printf("FAIL GetExitCodeThread\n");
        return 1;
    }
    if (code != 7) {
        printf("FAIL exit code=%u\n", (unsigned)code);
        return 1;
    }
    if (g_shared != 100) {
        printf("FAIL shared=%d\n", g_shared);
        return 1;
    }
    DeleteCriticalSection(&g_cs);
    CloseHandle(h);
    printf("libWINAPI exitcode+cs: ok\n");
    return 0;
}
