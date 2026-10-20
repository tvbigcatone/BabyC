/* Integration regression: windows.h dispatches to libWINAPI on Linux. */
#include <windows.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    const char path[] = "babyc-winapi-smoke.tmp";
    const char payload[] = "WinAPI bridge: ok\n";
    char buffer[64];
    char envbuf[64];
    char tempbuf[260];
    char modulebuf[1024];
    char hostbuf[256];
    char userbuf[256];
    char cwdbuf[1024];
    char drives[1024];
    ULARGE_INTEGER free_bytes, total_bytes, total_free_bytes;
    DWORD transferred = 0;
    DWORD one_read = 0;
    char first_byte = '\0';
    HANDLE file;
    HANDLE out;
    WIN32_FIND_DATA find_data;
    SYSTEMTIME system_time;
    SYSTEMTIME local_time;
    LARGE_INTEGER perf_counter;
    LARGE_INTEGER perf_frequency;
    LARGE_INTEGER file_size;
    FILETIME idle_time;
    FILETIME kernel_time;
    FILETIME user_time;
    HANDLE find_handle;
    int envlen;
    char error_text[128];
    MEMORYSTATUSEX memory_status;
    HANDLE event_handle;
    HANDLE event_open_handle;
    HANDLE mutex_handle;
    HANDLE mapping_handle;
    char *mapped;

    GetSystemTime(&system_time);
    GetLocalTime(&local_time);
    if (system_time.wYear < 2020 || system_time.wMonth < 1 || system_time.wMonth > 12) return 36;
    if (local_time.wYear < 2020 || local_time.wMonth < 1 || local_time.wMonth > 12) return 37;
    if (!QueryPerformanceCounter(&perf_counter) || perf_counter.QuadPart <= 0) return 38;
    if (!QueryPerformanceFrequency(&perf_frequency) || perf_frequency.QuadPart <= 0) return 39;
    if (!GetSystemTimes(&idle_time, &kernel_time, &user_time)) return 40;
    (void)GetTickCount();
    if (GetLastError() != ERROR_SUCCESS) return 1;
    if (ERROR_ACCESS_DENIED != 5 || ERROR_NO_MORE_FILES != 18) return 35;
    if (getpid() <= 0 || GetCurrentProcessId() <= 0) return 28;
    SetLastError(ERROR_INVALID_PARAMETER);
    if (GetLastError() != ERROR_INVALID_PARAMETER) return 2;

    memset(error_text, 0, sizeof(error_text));
    if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                      NULL, ERROR_ACCESS_DENIED, 0, error_text,
                      sizeof(error_text), NULL) == 0) return 41;
    if (strstr(error_text, "access") == NULL && strstr(error_text, "Access") == NULL) return 42;
    memset(&memory_status, 0, sizeof(memory_status));
    memory_status.dwLength = sizeof(memory_status);
    if (!GlobalMemoryStatusEx(&memory_status) || memory_status.ullTotalPhys == 0 ||
        memory_status.ullAvailPhys == 0 || memory_status.dwMemoryLoad > 100) return 43;
    memset(drives, 0, sizeof(drives));
    if (GetLogicalDriveStrings(sizeof(drives), drives) == 0 || drives[0] == '\0') return 57;
    if (!GetDiskFreeSpaceEx(NULL, &free_bytes, &total_bytes, &total_free_bytes) ||
        total_bytes.QuadPart == 0 || total_free_bytes.QuadPart > total_bytes.QuadPart ||
        free_bytes.QuadPart > total_bytes.QuadPart) return 58;

    event_handle = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (event_handle == NULL) return 44;
    if (WaitForSingleObject(event_handle, 0) != WAIT_TIMEOUT) { CloseHandle(event_handle); return 45; }
    if (!SetEvent(event_handle) || WaitForSingleObject(event_handle, 0) != WAIT_OBJECT_0) { CloseHandle(event_handle); return 46; }
    if (!ResetEvent(event_handle) || WaitForSingleObject(event_handle, 0) != WAIT_TIMEOUT) { CloseHandle(event_handle); return 47; }
    if (!CloseHandle(event_handle)) return 48;

    mutex_handle = CreateMutex(NULL, FALSE, NULL);
    if (mutex_handle == NULL) return 49;
    if (WaitForSingleObject(mutex_handle, 0) != WAIT_OBJECT_0) { CloseHandle(mutex_handle); return 50; }
    if (!ReleaseMutex(mutex_handle) || !CloseHandle(mutex_handle)) return 51;

    file = CreateFile(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 3;
    if (!WriteFile(file, payload, (DWORD)strlen(payload), &transferred, NULL)) {
        CloseHandle(file); return 4;
    }
    if (transferred != (DWORD)strlen(payload)) { CloseHandle(file); return 5; }
    if (!CloseHandle(file)) return 6;

    file = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 52;
    mapping_handle = CreateFileMapping(file, NULL, PAGE_READWRITE, 0, 0, NULL);
    if (mapping_handle == NULL) { CloseHandle(file); return 53; }
    mapped = (char *)MapViewOfFile(mapping_handle, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (mapped == NULL) { CloseHandle(mapping_handle); CloseHandle(file); return 54; }
    if (strcmp(mapped, payload) != 0) { UnmapViewOfFile(mapped); CloseHandle(mapping_handle); CloseHandle(file); return 55; }
    if (!UnmapViewOfFile(mapped) || !CloseHandle(mapping_handle) || !CloseHandle(file)) return 56;

    file = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                      FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) return 7;
    memset(buffer, 0, sizeof(buffer));
    transferred = 0;
    if (!ReadFile(file, buffer, sizeof(buffer) - 1, &transferred, NULL)) {
        CloseHandle(file); return 8;
    }
    if (transferred != (DWORD)strlen(payload)) { CloseHandle(file); return 10; }
    if (strcmp(buffer, payload) != 0) { CloseHandle(file); return 11; }
    if (GetFileSize(file, NULL) != (DWORD)strlen(payload)) { CloseHandle(file); return 31; }
    file_size.QuadPart = -1;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart != (long long)strlen(payload)) { CloseHandle(file); return 59; }
    /* Metadata queries must not change the current file position. */
    if (SetFilePointer(file, 0, NULL, FILE_BEGIN) != 0) { CloseHandle(file); return 32; }
    file_size.QuadPart = -1;
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart != (long long)strlen(payload)) { CloseHandle(file); return 60; }
    if (!ReadFile(file, &first_byte, 1, &one_read, NULL) || one_read != 1 || first_byte != payload[0]) { CloseHandle(file); return 61; }
    /* Invalid handles and null output pointers fail cleanly instead of crashing. */
    if (GetFileSizeEx(INVALID_HANDLE_VALUE, &file_size) != FALSE) { CloseHandle(file); return 62; }
    if (GetFileSizeEx(file, NULL) != FALSE) { CloseHandle(file); return 63; }
    if (!CloseHandle(file)) return 9;

    find_handle = FindFirstFile(path, &find_data);
    if (find_handle == INVALID_HANDLE_VALUE) return 14;
    if (FindClose(find_handle) == FALSE) return 15;

    if (!CopyFile(path, "babyc-winapi-copy.tmp", 1)) return 16;
    if (!MoveFile("babyc-winapi-copy.tmp", "babyc-winapi-moved.tmp")) return 17;
    if (!DeleteFile("babyc-winapi-moved.tmp")) return 18;
    if (!CreateDirectory("babyc-winapi-dir.tmp")) return 19;
    if (!RemoveDirectory("babyc-winapi-dir.tmp")) return 20;

    if (!SetEnvironmentVariable("BABYC_WINAPI_TEST", "ok")) return 21;
    memset(envbuf, 0, sizeof(envbuf));
    envlen = GetEnvironmentVariable("BABYC_WINAPI_TEST", envbuf, sizeof(envbuf));
    if (envlen != 2 || strcmp(envbuf, "ok") != 0) return 22;
    if (!SetEnvironmentVariable("BABYC_WINAPI_TEST", NULL)) return 23;
    if (GetTempPath(sizeof(tempbuf), tempbuf) <= 0 || tempbuf[0] == '\0') return 24;
    if (GetModuleFileName(modulebuf, sizeof(modulebuf)) <= 0 || modulebuf[0] == '\0') return 25;
    if (GetCurrentDirectory(sizeof(cwdbuf), cwdbuf) <= 0 || cwdbuf[0] == '\0') return 33;
    if (!SetCurrentDirectory(cwdbuf)) return 34;
    if (GetComputerName(hostbuf, sizeof(hostbuf)) <= 0 || hostbuf[0] == '\0') return 26;
    if (GetUserName(userbuf, sizeof(userbuf)) <= 0 || userbuf[0] == '\0') return 27;
    if (ShowWindow(NULL, SW_SHOW) != FALSE) return 29;
    if (GetLastError() != ERROR_CALL_NOT_IMPLEMENTED) return 30;

    remove(path);
    out = GetStdHandle(STD_OUTPUT_HANDLE);
    transferred = 0;
    if (!WriteFile(out, payload, (DWORD)strlen(payload), &transferred, NULL)) return 12;
    if (transferred != (DWORD)strlen(payload)) return 13;
    return 0;
}
