#include <windows.h>
#include <stdio.h>

int main(void)
{
    FILETIME epoch, now, local, utc, left, right;
    SYSTEMTIME st, leap;
    HANDLE file, search;
    FILETIME creation, access_time, write_time;
    WIN32_FIND_DATA data;
    const char *path = "tests/windows/winapi_filetime.c";

    epoch.dwLowDateTime = 0xd53e8000u;
    epoch.dwHighDateTime = 0x019db1deu;
    if (!FileTimeToSystemTime(&epoch, &st) ||
        st.wYear != 1970 || st.wMonth != 1 || st.wDay != 1 || st.wDayOfWeek != 4)
        return 1;

    leap.wYear = 2000;
    leap.wMonth = 2;
    leap.wDayOfWeek = 6; /* ignored by SystemTimeToFileTime */
    leap.wDay = 29;
    leap.wHour = 23;
    leap.wMinute = 59;
    leap.wSecond = 59;
    leap.wMilliseconds = 999;
    if (!SystemTimeToFileTime(&leap, &left) || !FileTimeToSystemTime(&left, &st) ||
        st.wYear != 2000 || st.wMonth != 2 || st.wDay != 29 ||
        st.wDayOfWeek != 2 || st.wHour != 23 || st.wSecond != 59 ||
        st.wMilliseconds != 999)
        return 2;

    right.dwLowDateTime = 0;
    right.dwHighDateTime = 1;
    left.dwLowDateTime = 0xffffffffu;
    left.dwHighDateTime = 0;
    if (CompareFileTime(&left, &right) != -1 ||
        CompareFileTime(&epoch, &epoch) != 0)
        return 3;

    st.wYear = 2000; st.wMonth = 13; st.wDay = 1;
    st.wHour = 0; st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
    if (SystemTimeToFileTime(&st, &left) || GetLastError() != ERROR_INVALID_PARAMETER)
        return 4;
    left.dwLowDateTime = 0;
    left.dwHighDateTime = 0x80000000u;
    if (FileTimeToSystemTime(&left, &st) || GetLastError() != ERROR_INVALID_PARAMETER)
        return 5;

    GetSystemTimeAsFileTime(&now);
    if (!FileTimeToSystemTime(&now, &st) || st.wYear < 2020 || st.wYear > 2100)
        return 6;
    if (!FileTimeToLocalFileTime(&epoch, &local) ||
        !LocalFileTimeToFileTime(&local, &utc) || CompareFileTime(&epoch, &utc) != 0)
        return 7;

    file = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE || file == NULL)
        return 8;
    if (!GetFileTime(file, &creation, &access_time, &write_time) ||
        !GetFileTime(file, NULL, NULL, NULL))
        return 9;
    if ((access_time.dwLowDateTime == 0 && access_time.dwHighDateTime == 0) ||
        (write_time.dwLowDateTime == 0 && write_time.dwHighDateTime == 0))
        return 10;
    search = FindFirstFile(path, &data);
    if (search == INVALID_HANDLE_VALUE || search == NULL)
        return 11;
    if ((data.ftLastWriteTime.dwLowDateTime == 0 && data.ftLastWriteTime.dwHighDateTime == 0) ||
        !FindClose(search))
        return 12;
    if (!CloseHandle(file))
        return 13;
    if (GetFileTime(file, &creation, NULL, NULL) || GetLastError() != ERROR_INVALID_HANDLE)
        return 14;

    puts("WinAPI FILETIME: ok");
    return 0;
}
