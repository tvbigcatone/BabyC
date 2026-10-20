
BOOL WINAPI GetSystemTimes(
  _Out_opt_ LPFILETIME lpIdleTime,
  _Out_opt_ LPFILETIME lpKernelTime,
  _Out_opt_ LPFILETIME lpUserTime
);

BOOL WINAPI QueryPerformanceCounter(
  _Out_ LARGE_INTEGER *lpPerformanceCount
);

BOOL WINAPI QueryPerformanceFrequency(
  _Out_ LARGE_INTEGER *lpFrequency
);

VOID WINAPI Sleep(
  _In_ DWORD dwMilliseconds
);