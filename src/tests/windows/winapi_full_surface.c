/* Every libWINAPI API registered for BabyC must be callable (complete bridge). */
#include <stdio.h>
#include <windows.h>

static int g = 0;
DWORD WINAPI worker(LPVOID arg)
{
    (void)arg;
    g = 1;
    return 42;
}

int main(void)
{
    HANDLE h;
    HANDLE handles[1];
    DWORD tid = 0, code = 0, wait, n;
    UINT dt;
    CRITICAL_SECTION cs;
    void *start = worker;
    BOOL ok;

    InitializeCriticalSection(&cs);
    EnterCriticalSection(&cs);
    LeaveCriticalSection(&cs);
    DeleteCriticalSection(&cs);

    h = CreateThread(0, 0, start, 0, 0, &tid);
    if (h == 0) { printf("FAIL create\n"); return 1; }
    wait = WaitForSingleObject(h, 5000);
    if (wait != 0) { printf("FAIL wait single\n"); return 1; }
    ok = GetExitCodeThread(h, &code);
    if (!ok || code != 42 || g != 1) {
        printf("FAIL exit code ok=%d code=%u g=%d\n", ok, (unsigned)code, g);
        return 1;
    }

    handles[0] = h;
    n = WaitForMultipleObjects(1, handles, 1, 0);
    (void)n;

    /* These may return failure/not-implemented on the host; they must resolve. */
    (void)SetThreadPriority(h, 0);
    (void)PulseEvent(h); /* invalid handle type is ok for symbol smoke */
    (void)SuspendThread(h);
    (void)ResumeThread(h);
    /* TerminateThread after join is best-effort; do not require success */
    (void)TerminateThread(h, 0);

    dt = GetDriveType("/");
    (void)dt;

    CloseHandle(h);
    printf("libWINAPI full surface: ok\n");
    return 0;
}
