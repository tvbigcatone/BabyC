
typedef struct _SYSTEMTIME {
  WORD wYear;
  WORD wMonth;
  WORD wDayOfWeek;
  WORD wDay;
  WORD wHour;
  WORD wMinute;
  WORD wSecond;
  WORD wMilliseconds;
} SYSTEMTIME, *PSYSTEMTIME, *LPSYSTEMTIME;

void WINAPI GetSystemTimeAsFileTime(
  _Out_ LPFILETIME lpSystemTimeAsFileTime
);

BOOL WINAPI SystemTimeToFileTime(
  _In_ const SYSTEMTIME *lpSystemTime,
  _Out_ LPFILETIME lpFileTime
);

BOOL WINAPI FileTimeToSystemTime(
  _In_ const FILETIME *lpFileTime,
  _Out_ LPSYSTEMTIME lpSystemTime
);

LONG WINAPI CompareFileTime(
  _In_ const FILETIME *lpFileTime1,
  _In_ const FILETIME *lpFileTime2
);

BOOL WINAPI FileTimeToLocalFileTime(
  _In_ const FILETIME *lpFileTime,
  _Out_ LPFILETIME lpLocalFileTime
);

BOOL WINAPI LocalFileTimeToFileTime(
  _In_ const FILETIME *lpLocalFileTime,
  _Out_ LPFILETIME lpFileTime
);

void WINAPI GetSystemTime(
  _Out_ LPSYSTEMTIME lpSystemTime
);

void WINAPI GetLocalTime(
  _Out_ LPSYSTEMTIME lpSystemTime
);

BOOL WINAPI SetLocalTime(
  _In_ const SYSTEMTIME *lpSystemTime
);