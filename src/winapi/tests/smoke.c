#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <sched.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <time.h>
#include "windows.h"

/* Internal CRT wildcard matcher: exercised directly for adversarial patterns. */
extern int match_spec(const char *spec, const char *text);

#if defined(ENABLE_WSHMEM) && ENABLE_WSHMEM && defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
extern void winapi_test_shmem_fail_pwrite_after(ssize_t bytes);
extern void winapi_test_shmem_fail_rollback_ftruncate(void);
extern void winapi_test_shmem_fail_next_pread(void);
extern void winapi_test_shmem_fail_next_fchmod(void);
extern void winapi_test_shmem_fail_next_shm_open_create(void);
extern void winapi_test_shmem_fail_next_flock_ex(void);
extern void winapi_test_shmem_fail_next_flock_un(void);
extern void winapi_test_shmem_fail_next_shm_unlink(void);
extern void winapi_test_shmem_fail_next_calloc(void);
extern void winapi_test_shmem_fail_calloc_after(long successful_calls);
extern void winapi_test_shmem_fail_fstat_after(long successful_calls);
#endif

/* Use the POSIX mkdir(2) for private runtime coordination directories. */
#ifdef mkdir
#undef mkdir
#endif


#if defined(ENABLE_WTHREADS) && ENABLE_WTHREADS
typedef struct wait_test_args { HANDLE event; DWORD result; } wait_test_args;
static DWORD WINAPI signal_event_worker(LPVOID arg)
{
   wait_test_args *a = (wait_test_args *)arg;
   Sleep(25);
   assert(SetEvent(a->event) == TRUE);
   return 23;
}
static DWORD WINAPI wait_event_worker(LPVOID arg)
{
   wait_test_args *a = (wait_test_args *)arg;
   a->result = WaitForSingleObject(a->event, 1000);
   return 0;
}
static DWORD WINAPI wait_event_long_worker(LPVOID arg)
{
   wait_test_args *a = (wait_test_args *)arg;
   a->result = WaitForSingleObject(a->event, 2000);
   return 0;
}
static DWORD WINAPI abandon_mutex_worker(LPVOID arg)
{
   assert(WaitForSingleObject((HANDLE)arg, 1000) == WAIT_OBJECT_0);
   /* Deliberately exit while owning the mutex to test abandonment handling. */
   return 0;
}
static DWORD WINAPI return_high_exit_code(LPVOID arg)
{
   (void)arg;
   return (DWORD)0xF1234567u;
}
static DWORD WINAPI return_zero(LPVOID arg)
{
   (void)arg;
   return 0;
}

typedef struct stress_args {
   HANDLE mutex;
   unsigned int iterations;
   unsigned long *counter;
} stress_args;

static DWORD WINAPI concurrent_handle_stress(LPVOID arg)
{
   stress_args *a = (stress_args *)arg;
   for (unsigned int i = 0; i < a->iterations; ++i) {
      HANDLE ev = CreateEvent(NULL, (i & 1u) != 0, FALSE, NULL);
      assert(ev != NULL);
      assert(SetEvent(ev) == TRUE);
      assert(WaitForSingleObject(ev, 0) == WAIT_OBJECT_0);
      if ((i & 1u) == 0)
         assert(WaitForSingleObject(ev, 0) == WAIT_TIMEOUT);
      assert(CloseHandle(ev) == TRUE);

      assert(WaitForSingleObject(a->mutex, INFINITE) == WAIT_OBJECT_0);
      ++(*a->counter);
      assert(ReleaseMutex(a->mutex) == TRUE);
   }
   return 0;
}
#endif

typedef struct file_close_race_args {
   HANDLE handle;
   atomic_uint successful_reads;
} file_close_race_args;

static void *file_close_race_worker(void *opaque)
{
   file_close_race_args *args = (file_close_race_args *)opaque;
   for (;;) {
      LARGE_INTEGER start = { .QuadPart = 0 };
      char byte = 0;
      DWORD count = 0;
      if (!SetFilePointerEx(args->handle, start, NULL, 0u)) {
         if (GetLastError() == ERROR_INVALID_HANDLE) break;
         assert(!"unexpected seek failure in close race");
      }
      if (ReadFile(args->handle, &byte, 1, &count, NULL)) {
         /* Synchronous EOF is TRUE with zero bytes; another worker may move
          * the shared file position between this worker's seek and read. */
         if (count == 0) continue;
         assert(count == 1);
         atomic_fetch_add_explicit(&args->successful_reads, 1, memory_order_relaxed);
      } else if (GetLastError() == ERROR_INVALID_HANDLE) {
         break;
      } else {
         assert(!"unexpected read failure in close race");
      }
   }
   return NULL;
}

typedef struct file_handle_stress_args { unsigned int worker_id; unsigned int iterations; } file_handle_stress_args;
static void *file_handle_stress_worker(void *arg)
{
   file_handle_stress_args *a = (file_handle_stress_args *)arg;
   for (unsigned int i = 0; i < a->iterations; ++i) {
      char path[160];
      int n = snprintf(path, sizeof(path), "/tmp/winapi-file-handle-%ld-%u-%u",
                       (long)getpid(), a->worker_id, i);
      assert(n > 0 && (size_t)n < sizeof(path));
      (void)unlink(path);
      HANDLE h = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(h != INVALID_HANDLE_VALUE && h != NULL);
      assert(CloseHandle(h) == TRUE);
      assert(unlink(path) == 0);
   }
   return NULL;
}

/* Start FindNextFile and FindClose together without polling.  The registry
 * must serialize the lookup/use/removal so no worker can dereference a freed
 * search entry.  A Next call may win before close or observe the closed token. */
typedef struct find_race_gate {
   pthread_mutex_t mutex;
   pthread_cond_t cond;
   unsigned int ready;
   unsigned int total;
   int released;
} find_race_gate;

typedef struct find_race_args {
   find_race_gate *gate;
   HANDLE handle;
   int close_operation;
   BOOL result;
   DWORD last_error;
} find_race_args;

static void *find_next_close_race_worker(void *opaque)
{
   find_race_args *args = (find_race_args *)opaque;
   WIN32_FIND_DATA data;
   assert(pthread_mutex_lock(&args->gate->mutex) == 0);
   ++args->gate->ready;
   if (args->gate->ready == args->gate->total) {
      args->gate->released = 1;
      assert(pthread_cond_broadcast(&args->gate->cond) == 0);
   }
   while (!args->gate->released)
      assert(pthread_cond_wait(&args->gate->cond, &args->gate->mutex) == 0);
   assert(pthread_mutex_unlock(&args->gate->mutex) == 0);

   if (args->close_operation) {
      args->result = FindClose(args->handle);
   } else {
      SetLastError(ERROR_SUCCESS);
      args->result = FindNextFile(args->handle, &data);
      args->last_error = GetLastError();
   }
   return NULL;
}

static void *worker(void *arg)
{
   (void)arg;
   /* l'errore e' per-thread: qui parte da 0 anche se il main ha impostato 5 */
   assert(GetLastError() == 0);
   SetLastError(7);
   assert(GetLastError() == 7);
   return NULL;
}

#if defined(ENABLE_WSHMEM) && ENABLE_WSHMEM
typedef struct shmem_view_stress_args {
   HANDLE mapping;
   unsigned int iterations;
   unsigned int worker_id;
} shmem_view_stress_args;

static void *shmem_view_stress_worker(void *opaque)
{
   shmem_view_stress_args *args = (shmem_view_stress_args *)opaque;
   for (unsigned int i = 0; i < args->iterations; ++i) {
      unsigned char *view = (unsigned char *)MapViewOfFile(args->mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      assert(view != NULL);
      view[(args->worker_id * 97u + i) % 4096u] = (unsigned char)(args->worker_id + i);
      assert(UnmapViewOfFile(view) == TRUE);
   }
   return NULL;
}

typedef struct shmem_create_gate {
   pthread_mutex_t mutex;
   pthread_cond_t cond;
   unsigned int ready;
   unsigned int total;
   int released;
} shmem_create_gate;

typedef struct shmem_create_race_args {
   shmem_create_gate *gate;
   const char *name;
   HANDLE mapping;
   DWORD last_error;
} shmem_create_race_args;

/* A condition-variable gate synchronizes the start without polling. */
static void *shmem_create_race_worker(void *opaque)
{
   shmem_create_race_args *args = (shmem_create_race_args *)opaque;
   assert(pthread_mutex_lock(&args->gate->mutex) == 0);
   ++args->gate->ready;
   if (args->gate->ready == args->gate->total) {
      args->gate->released = 1;
      assert(pthread_cond_broadcast(&args->gate->cond) == 0);
   }
   while (!args->gate->released)
      assert(pthread_cond_wait(&args->gate->cond, &args->gate->mutex) == 0);
   assert(pthread_mutex_unlock(&args->gate->mutex) == 0);

   args->mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
      0, 4096, args->name);
   args->last_error = GetLastError();
   return NULL;
}

typedef struct shmem_close_race_args {
   HANDLE mapping;
   unsigned int iterations;
   atomic_uint attempts;
} shmem_close_race_args;

/* A separately pinned view makes every concurrent close deterministically
 * return ERROR_BUSY while workers mutate the view registry. */
static void *shmem_close_race_worker(void *opaque)
{
   shmem_close_race_args *args = (shmem_close_race_args *)opaque;
   for (unsigned int i = 0; i < args->iterations; ++i) {
      assert(CloseHandle(args->mapping) == FALSE);
      assert(GetLastError() == ERROR_BUSY);
      atomic_fetch_add_explicit(&args->attempts, 1u, memory_order_relaxed);
   }
   return NULL;
}
#endif

static uint64_t smoke_filetime_ticks(const FILETIME *ft)
{
   return ((uint64_t)ft->dwHighDateTime << 32) | (uint64_t)ft->dwLowDateTime;
}

static uint64_t smoke_expected_filetime(time_t seconds, long nanoseconds)
{
   return ((uint64_t)((int64_t)seconds + INT64_C(11644473600)) * UINT64_C(10000000)) +
      (uint64_t)nanoseconds / UINT64_C(100);
}

static void test_filetime_family(void)
{
   FILETIME ft, converted, local_ft, access_time, write_time;
   SYSTEMTIME st, roundtrip;
   SYSTEMTIME input = { 2000, 2, 6, 29, 23, 59, 59, 999 };
   FILETIME epoch_1970 = { UINT32_C(0xd53e8000), UINT32_C(0x019db1de) };
   FILETIME epoch_1601 = { 0u, 0u };
   FILETIME max_signed = { UINT32_MAX, UINT32_C(0x7fffffff) };
   FILETIME one_high = { 0u, 1u };
   FILETIME max_low = { UINT32_MAX, 0u };
   time_t before, after, unix_seconds;
   char path[] = "/tmp/winapi-filetime-XXXXXX";
   int raw_fd;
   struct timespec timestamps[2];
   HANDLE file_handle;
   WIN32_FIND_DATA find_data;
   HANDLE find_handle;
   const char *old_tz_value = getenv("TZ");
   char *old_tz = old_tz_value != NULL ? strdup(old_tz_value) : NULL;

   assert(old_tz_value == NULL || old_tz != NULL);

   /* Documented epoch vectors, including day-of-week (Sunday == 0). */
   assert(FileTimeToSystemTime(&epoch_1970, &st) == TRUE);
   assert(st.wYear == 1970 && st.wMonth == 1 && st.wDay == 1);
   assert(st.wHour == 0 && st.wMinute == 0 && st.wSecond == 0 && st.wMilliseconds == 0);
   assert(st.wDayOfWeek == 4);
   st.wYear = 1970; st.wMonth = 1; st.wDay = 1; st.wHour = 0;
   st.wMinute = 0; st.wSecond = 0; st.wMilliseconds = 0;
   st.wDayOfWeek = 99;
   assert(SystemTimeToFileTime(&st, &converted) == TRUE);
   assert(smoke_filetime_ticks(&converted) == UINT64_C(116444736000000000));
   assert(FileTimeToSystemTime(&epoch_1601, &st) == TRUE);
   assert(st.wYear == 1601 && st.wMonth == 1 && st.wDay == 1);
   assert(st.wHour == 0 && st.wMinute == 0 && st.wSecond == 0 && st.wMilliseconds == 0);
   assert(st.wDayOfWeek == 1);
   assert(SystemTimeToFileTime(&st, &converted) == TRUE);
   assert(smoke_filetime_ticks(&converted) == 0u);
   assert(FileTimeToSystemTime(&max_signed, &st) == TRUE);
   assert(st.wYear == 30828 && st.wMonth == 9 && st.wDay == 14);
   assert(st.wHour == 2 && st.wMinute == 48 && st.wSecond == 5 && st.wMilliseconds == 477);

   /* Millisecond round-trip of leap day; input wDayOfWeek is ignored. */
   assert(SystemTimeToFileTime(&input, &ft) == TRUE);
   assert(FileTimeToSystemTime(&ft, &roundtrip) == TRUE);
   assert(roundtrip.wYear == 2000 && roundtrip.wMonth == 2 && roundtrip.wDay == 29);
   assert(roundtrip.wDayOfWeek == 2 && roundtrip.wHour == 23 && roundtrip.wMinute == 59);
   assert(roundtrip.wSecond == 59 && roundtrip.wMilliseconds == 999);
   assert(smoke_filetime_ticks(&ft) == smoke_expected_filetime((time_t)951868799, 999000000L));

   /* All specified invalid SYSTEMTIME fields must set ERROR_INVALID_PARAMETER. */
   st = input; st.wMonth = 13;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input; st.wDay = 30;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input; st.wYear = 2001; st.wDay = 29;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input; st.wHour = 24;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input; st.wMilliseconds = 1000;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input; st.wYear = 1600;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input; st.wYear = 30828;
   assert(SystemTimeToFileTime(&st, &converted) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);
   st = input;
   assert(SystemTimeToFileTime(&st, &converted) == TRUE);
   assert(CompareFileTime(&epoch_1601, &epoch_1970) == -1);
   assert(CompareFileTime(&epoch_1970, &epoch_1970) == 0);
   assert(CompareFileTime(&epoch_1970, &max_signed) == -1);
   assert(CompareFileTime(&one_high, &max_low) == 1);
   assert(CompareFileTime(&one_high, &one_high) == 0);
   ft.dwHighDateTime = UINT32_C(0x80000000); ft.dwLowDateTime = 0;
   assert(FileTimeToSystemTime(&ft, &st) == FALSE && GetLastError() == ERROR_INVALID_PARAMETER);

   /* Both local conversion functions use the offset in force at conversion time. */
   assert(setenv("TZ", "UTC", 1) == 0);
   tzset();
   assert(FileTimeToLocalFileTime(&epoch_1970, &local_ft) == TRUE);
   assert(smoke_filetime_ticks(&local_ft) == smoke_filetime_ticks(&epoch_1970));
   assert(LocalFileTimeToFileTime(&local_ft, &converted) == TRUE);
   assert(smoke_filetime_ticks(&converted) == smoke_filetime_ticks(&epoch_1970));
   assert(setenv("TZ", "Europe/Rome", 1) == 0);
   tzset();
   assert(FileTimeToLocalFileTime(&epoch_1970, &local_ft) == TRUE);
   {
      uint64_t utc_ticks = smoke_filetime_ticks(&epoch_1970);
      uint64_t local_ticks = smoke_filetime_ticks(&local_ft);
      uint64_t delta = local_ticks > utc_ticks ? local_ticks - utc_ticks : utc_ticks - local_ticks;
      assert(delta == UINT64_C(3600) * UINT64_C(10000000) ||
             delta == UINT64_C(7200) * UINT64_C(10000000));
   }
   assert(LocalFileTimeToFileTime(&local_ft, &converted) == TRUE);
   assert(smoke_filetime_ticks(&converted) == smoke_filetime_ticks(&epoch_1970));
   {
      SYSTEMTIME summer = { 0 }, winter = { 0 };
      FILETIME summer_utc, winter_utc, summer_local, winter_local;
      uint64_t summer_delta, winter_delta;
      summer.wYear = 2020; summer.wMonth = 7; summer.wDay = 1;
      winter.wYear = 2020; winter.wMonth = 1; winter.wDay = 1;
      assert(SystemTimeToFileTime(&summer, &summer_utc) == TRUE);
      assert(SystemTimeToFileTime(&winter, &winter_utc) == TRUE);
      assert(FileTimeToLocalFileTime(&summer_utc, &summer_local) == TRUE);
      assert(FileTimeToLocalFileTime(&winter_utc, &winter_local) == TRUE);
      summer_delta = smoke_filetime_ticks(&summer_local) - smoke_filetime_ticks(&summer_utc);
      winter_delta = smoke_filetime_ticks(&winter_local) - smoke_filetime_ticks(&winter_utc);
      /* The selected Win32-compatible policy applies today's bias to both
       * dates; it does not infer historical DST from each FILETIME. */
      assert(summer_delta == winter_delta);
      assert(LocalFileTimeToFileTime(&summer_local, &converted) == TRUE);
      assert(smoke_filetime_ticks(&converted) == smoke_filetime_ticks(&summer_utc));
      assert(LocalFileTimeToFileTime(&winter_local, &converted) == TRUE);
      assert(smoke_filetime_ticks(&converted) == smoke_filetime_ticks(&winter_utc));
   }
   if (old_tz != NULL) {
      assert(setenv("TZ", old_tz, 1) == 0);
      free(old_tz);
   } else {
      assert(unsetenv("TZ") == 0);
   }
   tzset();

   /* Current system time must be within two seconds of time(2). */
   before = time(NULL);
   GetSystemTimeAsFileTime(&ft);
   after = time(NULL);
   unix_seconds = (time_t)(smoke_filetime_ticks(&ft) / UINT64_C(10000000) -
      UINT64_C(11644473600));
   assert(unix_seconds >= before - 2 && unix_seconds <= after + 2);

   /* stat timestamps preserve nanoseconds down to 100ns through both APIs. */
   raw_fd = mkstemp(path);
   assert(raw_fd >= 0);
   assert(write(raw_fd, "filetime", 8) == 8);
   assert(close(raw_fd) == 0);
   timestamps[0].tv_sec = (time_t)946684800; /* 2000-01-01 UTC */
   timestamps[0].tv_nsec = 123456789L;
   timestamps[1].tv_sec = (time_t)951782400; /* 2000-02-29 UTC */
   timestamps[1].tv_nsec = 987654321L;
   assert(utimensat(AT_FDCWD, path, timestamps, 0) == 0);
   file_handle = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL, NULL);
   assert(file_handle != INVALID_HANDLE_VALUE && file_handle != NULL);
   assert(GetFileTime(file_handle, &ft, &access_time, &write_time) == TRUE);
   assert(smoke_filetime_ticks(&access_time) == smoke_expected_filetime(timestamps[0].tv_sec, timestamps[0].tv_nsec));
   assert(smoke_filetime_ticks(&write_time) == smoke_expected_filetime(timestamps[1].tv_sec, timestamps[1].tv_nsec));
   assert(GetFileTime(file_handle, NULL, NULL, &write_time) == TRUE);
   assert(smoke_filetime_ticks(&write_time) == smoke_expected_filetime(timestamps[1].tv_sec, timestamps[1].tv_nsec));
   assert(GetFileTime(file_handle, NULL, NULL, NULL) == TRUE);
   find_handle = FindFirstFile(path, &find_data);
   assert(find_handle != INVALID_HANDLE_VALUE && find_handle != NULL);
   assert(smoke_filetime_ticks(&find_data.ftLastAccessTime) == smoke_expected_filetime(timestamps[0].tv_sec, timestamps[0].tv_nsec));
   assert(smoke_filetime_ticks(&find_data.ftLastWriteTime) == smoke_expected_filetime(timestamps[1].tv_sec, timestamps[1].tv_nsec));
   assert(FindClose(find_handle) == TRUE);
   assert(CloseHandle(file_handle) == TRUE);
   assert(GetFileTime(file_handle, &ft, NULL, NULL) == FALSE && GetLastError() == ERROR_INVALID_HANDLE);
   assert(GetFileTime((HANDLE)(uintptr_t)0x1234, NULL, NULL, NULL) == FALSE && GetLastError() == ERROR_INVALID_HANDLE);
   assert(unlink(path) == 0);
}

int main(int argc, char **argv)
{
   test_filetime_family();
   /* The message-box backend only implements MB_OK; other button layouts must
    * not be reported as a successful dialog with the wrong return value. */
   assert(MessageBox(0, "text", "caption", MB_OKCANCEL) == 0);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
#if defined(ENABLE_DISPLAY) && ENABLE_DISPLAY
   assert(EnumDisplayDevices(NULL, 0, NULL, 0) == FALSE);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   {
      DEVMODE mode;
      memset(&mode, 0, sizeof(mode));
      assert(EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &mode) == FALSE);
      assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
      assert(ChangeDisplaySettings(&mode, 0) == DISP_CHANGE_FAILED);
      assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   }
#endif
   (void)argc;
   (void)argv;
   /* Wildcard matching must be iterative, null-safe and preserve CRT rules. */
   assert(match_spec("*.txt", "alpha.txt") == 1);
   assert(match_spec("*.txt", "alpha.bin") == 0);
   assert(match_spec("a?c", "abc") == 1);
   assert(match_spec("a?c", "ac") == 0);
   assert(match_spec("*.*", "README") == 1);
   assert(match_spec("", "") == 1);
   assert(match_spec(NULL, "anything") == 0);
   assert(match_spec("*", NULL) == 0);
   {
      char adversarial_pattern[256];
      char adversarial_text[2048];
      size_t i, pos = 0;
      for (i = 0; i < 40; ++i) {
         adversarial_pattern[pos++] = '*';
         adversarial_pattern[pos++] = 'a';
      }
      adversarial_pattern[pos++] = 'z';
      adversarial_pattern[pos] = '\0';
      memset(adversarial_text, 'b', sizeof(adversarial_text) - 1);
      adversarial_text[sizeof(adversarial_text) - 1] = '\0';
      assert(match_spec(adversarial_pattern, adversarial_text) == 0);
   }
   /* String helpers validate optional inputs and avoid empty-pattern loops. */
   {
      char *replaced = str_replace("a-b-a", "a", "xy");
      assert(replaced != NULL && strcmp(replaced, "xy-b-xy") == 0);
      free(replaced);
      replaced = str_replace("a", "a", "aa");
      assert(replaced != NULL && strcmp(replaced, "aa") == 0);
      free(replaced);
      replaced = str_replace("unchanged", "", "x");
      assert(replaced != NULL && strcmp(replaced, "unchanged") == 0);
      free(replaced);
      replaced = str_replace("unchanged", NULL, "x");
      assert(replaced != NULL && strcmp(replaced, "unchanged") == 0);
      free(replaced);
      assert(str_replace(NULL, "x", "y") == NULL);

      UNICODE_STRING unicode_string;
      RtlInitUnicodeString(&unicode_string, NULL);
      assert(unicode_string.Buffer == NULL);
      assert(unicode_string.Length == 0 && unicode_string.MaximumLength == 0);
      {
         static const WCHAR short_wide[] = L"abc";
         WCHAR long_wide[20000];
         for (size_t i = 0; i + 1 < sizeof(long_wide) / sizeof(long_wide[0]); ++i)
            long_wide[i] = L'x';
         long_wide[sizeof(long_wide) / sizeof(long_wide[0]) - 1] = L'\0';
         RtlInitUnicodeString(&unicode_string, short_wide);
         assert(unicode_string.Buffer == short_wide);
         assert(unicode_string.Length == 3 * sizeof(WCHAR));
         assert(unicode_string.MaximumLength == 4 * sizeof(WCHAR));
         RtlInitUnicodeString(&unicode_string, long_wide);
         assert(unicode_string.Buffer == long_wide);
         assert(unicode_string.Length <= USHRT_MAX - sizeof(WCHAR));
         assert(unicode_string.Length % sizeof(WCHAR) == 0);
         assert(unicode_string.MaximumLength == unicode_string.Length + sizeof(WCHAR));
      }
   }

   /* FormatMessage uses the caller's buffer and reports unsupported modes. */
   {
      char message[128];
      DWORD length;
      length = FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             NULL, ERROR_FILE_NOT_FOUND, 0, message,
                             (DWORD)sizeof(message), NULL);
      assert(length > 0);
      assert(strcmp(message, str_werror(ERROR_FILE_NOT_FOUND)) == 0);

      message[0] = 'x';
      assert(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, ERROR_FILE_NOT_FOUND,
                           0, message, 2, NULL) == 0);
      assert(message[0] == '\0');
      assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);

      assert(FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                           NULL, ERROR_FILE_NOT_FOUND, 0, message,
                           (DWORD)sizeof(message), NULL) == 0);
      assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

      assert(FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, 0xFFFFFFFFu,
                           0, message, (DWORD)sizeof(message), NULL) == 0);
      assert(GetLastError() == ERROR_INVALID_PARAMETER);
   }
   /* File handles are opaque library tokens, never raw POSIX descriptors. */
   {
      char path[] = "/tmp/winapi-createfile-XXXXXX";
      int raw = mkstemp(path);
      assert(raw >= 0);
      assert(close(raw) == 0);
      HANDLE fh = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(fh != INVALID_HANDLE_VALUE && fh != NULL);
      assert(CloseHandle(fh) == TRUE);
      assert(CloseHandle(fh) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      /* Reuse a kernel descriptor after closing the WinAPI handle. A stale
       * opaque token must not be mistaken for that descriptor or close it. */
      {
         int reused = open(path, O_RDWR | O_CLOEXEC);
         assert(reused >= 0);
         assert(CloseHandle(fh) == FALSE);
         assert(GetLastError() == ERROR_INVALID_HANDLE);
         assert(write(reused, "z", 1) == 1);
         assert(fcntl(reused, F_GETFD) >= 0);
         assert(close(reused) == 0);
      }
      HANDLE missing = CreateFile(path, GENERIC_READ, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(missing == INVALID_HANDLE_VALUE);
      assert(GetLastError() == ERROR_FILE_EXISTS);
      assert(unlink(path) == 0);
      HANDLE created = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(created != INVALID_HANDLE_VALUE && created != NULL);
      assert(GetLastError() == ERROR_SUCCESS);
      const char preserved[] = "do-not-truncate-with-read-only";
      DWORD preserved_written = 0;
      assert(WriteFile(created, preserved, (DWORD)(sizeof(preserved) - 1),
                       &preserved_written, NULL) == TRUE);
      assert(preserved_written == (DWORD)(sizeof(preserved) - 1));
      assert(CloseHandle(created) == TRUE);
      /* Linux permits O_RDONLY|O_TRUNC on some filesystems despite the
       * unspecified POSIX semantics; Win32 must reject it without data loss. */
      HANDLE invalid_truncate = CreateFile(path, GENERIC_READ, 0, NULL,
          CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(invalid_truncate == INVALID_HANDLE_VALUE);
      assert(GetLastError() == ERROR_ACCESS_DENIED);
      FILE *preserved_file = fopen(path, "rb");
      assert(preserved_file != NULL);
      char preserved_check[sizeof(preserved)];
      assert(fread(preserved_check, 1, sizeof(preserved) - 1, preserved_file) == sizeof(preserved) - 1);
      assert(memcmp(preserved_check, preserved, sizeof(preserved) - 1) == 0);
      assert(fclose(preserved_file) == 0);
      HANDLE truncated = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(truncated != INVALID_HANDLE_VALUE && truncated != NULL);
      assert(GetLastError() == ERROR_ALREADY_EXISTS);
      assert(CloseHandle(truncated) == TRUE);
      assert(unlink(path) == 0);
      /* OPEN_ALWAYS reports creation atomically and preserves existing data. */
      HANDLE opened_always = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
          NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(opened_always != INVALID_HANDLE_VALUE && opened_always != NULL);
      assert(GetLastError() == ERROR_SUCCESS);
      DWORD opened_always_written = 0;
      assert(WriteFile(opened_always, "keep", 4, &opened_always_written, NULL) == TRUE);
      assert(opened_always_written == 4);
      assert(CloseHandle(opened_always) == TRUE);
      opened_always = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
          NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(opened_always != INVALID_HANDLE_VALUE && opened_always != NULL);
      assert(GetLastError() == ERROR_ALREADY_EXISTS);
      assert(GetFileSizeEx(opened_always, &(LARGE_INTEGER){0}) == TRUE);
      {
         LARGE_INTEGER existing_size;
         assert(GetFileSizeEx(opened_always, &existing_size) == TRUE);
         assert(existing_size.QuadPart == 4);
      }
      assert(CloseHandle(opened_always) == TRUE);
      assert(unlink(path) == 0);
      /* CREATE_ALWAYS creates a missing file and reports ERROR_SUCCESS. */
      HANDLE created_always = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0,
          NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(created_always != INVALID_HANDLE_VALUE && created_always != NULL);
      assert(GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(created_always) == TRUE);
      assert(unlink(path) == 0);
      assert(CreateFile(path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE);
      assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
      assert(CreateFile(path, GENERIC_READ, 0, (LPSECURITY_ATTRIBUTES)(uintptr_t)1, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL) == INVALID_HANDLE_VALUE);
      assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
      /* Synchronous ReadFile/WriteFile use opaque handles and preserve access rights. */
      HANDLE writer = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                 CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(writer != INVALID_HANDLE_VALUE && writer != NULL);
      assert(GetFileType(writer) == FILE_TYPE_DISK);
      assert(GetLastError() == ERROR_SUCCESS);
      {
         BY_HANDLE_FILE_INFORMATION file_info;
         memset(&file_info, 0xA5, sizeof(file_info));
         assert(GetFileInformationByHandle(writer, &file_info) == TRUE);
         assert(GetLastError() == ERROR_SUCCESS);
         assert((file_info.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0);
         assert(file_info.nNumberOfLinks >= 1);
         assert(file_info.nFileIndexHigh != 0 || file_info.nFileIndexLow != 0);
         assert(file_info.nFileSizeHigh == 0 && file_info.nFileSizeLow == 0);
         assert(GetFileInformationByHandle(writer, NULL) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
      }
      const char payload[] = "winapi-file-io";
      DWORD transferred = 0;
      assert(WriteFile(writer, payload, (DWORD)(sizeof(payload) - 1), &transferred, NULL) == TRUE);
      assert(transferred == (DWORD)(sizeof(payload) - 1));
      assert(FlushFileBuffers(writer) == TRUE);
      assert(GetLastError() == ERROR_SUCCESS);
      {
         LARGE_INTEGER size, distance, position;
         DWORD high = 0;
         assert(GetFileSizeEx(writer, &size) == TRUE);
         assert(size.QuadPart == (LONGLONG)(sizeof(payload) - 1));
         assert(GetFileSize(writer, &high) == (DWORD)(sizeof(payload) - 1));
         assert(high == 0);
         distance.QuadPart = 0;
         assert(SetFilePointerEx(writer, distance, &position, FILE_BEGIN) == TRUE);
         assert(position.QuadPart == 0);
         assert(SetFilePointer(writer, 2, NULL, FILE_BEGIN) == 2);
         assert(SetFilePointer(writer, 0, NULL, FILE_CURRENT) == 2);
         assert(SetFilePointer(writer, 0, NULL, FILE_END) == (DWORD)(sizeof(payload) - 1));
         {
            LONG high_part = 1;
            assert(SetFilePointer(writer, 5, &high_part, FILE_BEGIN) == 5);
            assert(high_part == 1);
            distance.QuadPart = 0;
            assert(SetFilePointerEx(writer, distance, NULL, FILE_BEGIN) == TRUE);
         }
         distance.QuadPart = -1;
         assert(SetFilePointerEx(writer, distance, NULL, FILE_BEGIN) == FALSE);
         assert(GetLastError() == ERROR_NEGATIVE_SEEK);
         assert(SetFilePointerEx(writer, (LARGE_INTEGER){ .QuadPart = 0 }, NULL, 99) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(GetFileSizeEx(writer, NULL) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         /* SetEndOfFile truncates/extends at the current pointer and does not
          * silently accept a read-only handle. */
         distance.QuadPart = 5;
         assert(SetFilePointerEx(writer, distance, NULL, FILE_BEGIN) == TRUE);
         assert(SetEndOfFile(writer) == TRUE);
         assert(GetLastError() == ERROR_SUCCESS);
         assert(GetFileSizeEx(writer, &size) == TRUE && size.QuadPart == 5);
         distance.QuadPart = 8;
         assert(SetFilePointerEx(writer, distance, NULL, FILE_BEGIN) == TRUE);
         assert(SetEndOfFile(writer) == TRUE);
         assert(GetFileSizeEx(writer, &size) == TRUE && size.QuadPart == 8);
         distance.QuadPart = 5;
         assert(SetFilePointerEx(writer, distance, NULL, FILE_BEGIN) == TRUE);
         {
            char zero_fill[3] = {1, 1, 1};
            assert(ReadFile(writer, zero_fill, 3, &transferred, NULL) == TRUE);
            assert(transferred == 3);
            assert(zero_fill[0] == 0 && zero_fill[1] == 0 && zero_fill[2] == 0);
         }
         /* Restore the original payload for the following ReadFile tests. */
         distance.QuadPart = 0;
         assert(SetFilePointerEx(writer, distance, NULL, FILE_BEGIN) == TRUE);
         assert(WriteFile(writer, payload, (DWORD)(sizeof(payload) - 1),
                          &transferred, NULL) == TRUE);
         assert(transferred == (DWORD)(sizeof(payload) - 1));
      }
      {
         DWORD attrs = GetFileAttributes(path);
         WIN32_FILE_ATTRIBUTE_DATA attr_data;
         assert(attrs != INVALID_FILE_ATTRIBUTES);
         assert((attrs & FILE_ATTRIBUTE_ARCHIVE) != 0);
         assert(GetLastError() == ERROR_SUCCESS);
         memset(&attr_data, 0xA5, sizeof(attr_data));
         assert(GetFileAttributesEx(path, GetFileExInfoStandard, &attr_data) == TRUE);
         assert(GetLastError() == ERROR_SUCCESS);
         assert((attr_data.dwFileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0);
         assert(attr_data.nFileSizeHigh == 0);
         assert(attr_data.nFileSizeLow == (DWORD)(sizeof(payload) - 1));
         assert(smoke_filetime_ticks(&attr_data.ftLastWriteTime) != 0);
         /* SetFileTime updates only the timestamps POSIX can represent. The
          * fixed values include sub-second ticks to test 100 ns conversion. */
         {
            FILETIME requested_access = { 0x01234567u, 0x01D6E3A0u };
            FILETIME requested_write = { 0x07654321u, 0x01D6E3A1u };
            FILETIME got_access = {0}, got_write = {0};
            assert(SetFileTime(writer, NULL, &requested_access, &requested_write) == TRUE);
            assert(GetFileTime(writer, NULL, &got_access, &got_write) == TRUE);
            assert(smoke_filetime_ticks(&got_access) == smoke_filetime_ticks(&requested_access));
            assert(smoke_filetime_ticks(&got_write) == smoke_filetime_ticks(&requested_write));
            assert(SetFileTime(writer, NULL, NULL, NULL) == TRUE);
            assert(SetFileTime(writer, &requested_write, NULL, NULL) == FALSE);
            assert(GetLastError() == ERROR_NOT_SUPPORTED);
            /* Unsupported creation-time updates must not partially mutate the
             * access/write timestamps that this backend can represent. */
            memset(&got_access, 0, sizeof(got_access));
            memset(&got_write, 0, sizeof(got_write));
            assert(GetFileTime(writer, NULL, &got_access, &got_write) == TRUE);
            assert(smoke_filetime_ticks(&got_access) == smoke_filetime_ticks(&requested_access));
            assert(smoke_filetime_ticks(&got_write) == smoke_filetime_ticks(&requested_write));
            assert(SetFileTime(writer, NULL, NULL, &requested_write) == TRUE);
            /* SetFileInformationByHandle exposes the same representable
             * timestamp subset and supports setting EOF without moving the
             * shared file pointer. */
            {
               FILE_BASIC_INFO set_basic;
               FILE_END_OF_FILE_INFO set_eof;
               LARGE_INTEGER before_position, after_position, zero;
               zero.QuadPart = 0;
               assert(SetFilePointerEx(writer, zero, &before_position, FILE_CURRENT) == TRUE);
               memset(&set_basic, 0, sizeof(set_basic));
               set_basic.LastAccessTime.QuadPart =
                  (LONGLONG)smoke_filetime_ticks(&requested_access);
               set_basic.LastWriteTime.QuadPart =
                  (LONGLONG)smoke_filetime_ticks(&requested_write);
               assert(SetFileInformationByHandle(writer, FileBasicInfo, &set_basic,
                                                  (DWORD)sizeof(set_basic)) == TRUE);
               assert(GetFileTime(writer, NULL, &got_access, &got_write) == TRUE);
               assert(smoke_filetime_ticks(&got_access) == smoke_filetime_ticks(&requested_access));
               assert(smoke_filetime_ticks(&got_write) == smoke_filetime_ticks(&requested_write));
               set_eof.EndOfFile.QuadPart = (LONGLONG)(sizeof(payload) - 2);
               assert(SetFileInformationByHandle(writer, FileEndOfFileInfo, &set_eof,
                                                  (DWORD)sizeof(set_eof)) == TRUE);
               assert(GetFileSizeEx(writer, &after_position) == TRUE);
               assert(after_position.QuadPart == (LONGLONG)(sizeof(payload) - 2));
               assert(SetFilePointerEx(writer, zero, &after_position, FILE_CURRENT) == TRUE);
               assert(after_position.QuadPart == before_position.QuadPart);
               set_eof.EndOfFile.QuadPart = -1;
               assert(SetFileInformationByHandle(writer, FileEndOfFileInfo, &set_eof,
                                                  (DWORD)sizeof(set_eof)) == FALSE);
               assert(GetLastError() == ERROR_INVALID_PARAMETER);
               assert(SetFileInformationByHandle(writer, FileEndOfFileInfo, &set_eof,
                                                  (DWORD)(sizeof(set_eof) - 1)) == FALSE);
               assert(GetLastError() == ERROR_INVALID_PARAMETER);
               memset(&set_basic, 0, sizeof(set_basic));
               set_basic.CreationTime.QuadPart = 1;
               assert(SetFileInformationByHandle(writer, FileBasicInfo, &set_basic,
                                                  (DWORD)sizeof(set_basic)) == FALSE);
               assert(GetLastError() == ERROR_NOT_SUPPORTED);
               set_eof.EndOfFile.QuadPart = 1;
               assert(SetFileInformationByHandle((HANDLE)(uintptr_t)0x1234,
                                                  FileEndOfFileInfo, &set_eof,
                                                  (DWORD)sizeof(set_eof)) == FALSE);
               assert(GetLastError() == ERROR_INVALID_HANDLE);
               assert(SetFileInformationByHandle(writer, (FILE_INFO_BY_HANDLE_CLASS)999,
                                                  &set_eof, (DWORD)sizeof(set_eof)) == FALSE);
               assert(GetLastError() == ERROR_INVALID_PARAMETER);
               /* Restore the data for later read tests. */
               zero.QuadPart = 0;
               assert(SetFilePointerEx(writer, zero, NULL, FILE_BEGIN) == TRUE);
               assert(WriteFile(writer, payload, (DWORD)(sizeof(payload) - 1),
                                &transferred, NULL) == TRUE);
               assert(transferred == (DWORD)(sizeof(payload) - 1));
            }
            assert(SetFileTime((HANDLE)(uintptr_t)0x1234, NULL, NULL, &requested_write) == FALSE);
            assert(GetLastError() == ERROR_INVALID_HANDLE);
            assert(SetFileTime((HANDLE)(uintptr_t)0x1234, &requested_write, NULL, NULL) == FALSE);
            assert(GetLastError() == ERROR_INVALID_HANDLE);
         }
         assert(GetFileAttributesEx(path, GetFileExMaxInfoLevel, &attr_data) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(GetFileAttributesEx(path, GetFileExInfoStandard, NULL) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(GetFileAttributes("/definitely/not/a/babyc-file") == INVALID_FILE_ATTRIBUTES);
         assert(GetLastError() == ERROR_FILE_NOT_FOUND);
         assert(GetFileAttributesEx("/definitely/not/a/babyc-file", GetFileExInfoStandard,
                                    &attr_data) == FALSE);
         assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      }
      assert(CloseHandle(writer) == TRUE);

      HANDLE reader = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
      assert(reader != INVALID_HANDLE_VALUE && reader != NULL);
      char actual[sizeof(payload)] = {0};
      transferred = 99;
      assert(ReadFile(reader, actual, (DWORD)(sizeof(payload) - 1), &transferred, NULL) == TRUE);
      assert(transferred == (DWORD)(sizeof(payload) - 1));
      assert(memcmp(actual, payload, sizeof(payload) - 1) == 0);
      {
         BY_HANDLE_FILE_INFORMATION file_info;
         assert(GetFileInformationByHandle(reader, &file_info) == TRUE);
         assert(file_info.nFileSizeHigh == 0);
         assert(file_info.nFileSizeLow == (DWORD)(sizeof(payload) - 1));
         assert(file_info.ftLastWriteTime.dwHighDateTime != 0 ||
                file_info.ftLastWriteTime.dwLowDateTime != 0);
      }
      {
         FILE_BASIC_INFO basic_info;
         FILE_BASIC_INFO preserved_basic;
         FILE_STANDARD_INFO standard_info;
         FILE_ATTRIBUTE_TAG_INFO tag_info;
         FILE_ID_INFO id_info;
         FILE_ALIGNMENT_INFO alignment_info;
         unsigned char name_storage[4096];
         FILE_NAME_INFO *name_info = (FILE_NAME_INFO *)name_storage;
         /* These layouts are part of the public Win32-facing contract. */
         assert(sizeof(FILE_BASIC_INFO) == 40);
         assert(sizeof(FILE_STANDARD_INFO) == 24);
         assert(sizeof(FILE_ATTRIBUTE_TAG_INFO) == 8);
         assert(sizeof(FILE_ID_128) == 16);
         assert(sizeof(FILE_ID_INFO) == 24);
         assert(sizeof(FILE_ALIGNMENT_INFO) == 4);
         assert(offsetof(FILE_NAME_INFO, FileName) == sizeof(DWORD));
         unsigned char short_buffer[sizeof(FILE_BASIC_INFO)];
         memset(&basic_info, 0, sizeof(basic_info));
         assert(GetFileInformationByHandleEx(reader, FileBasicInfo, &basic_info,
                                              (DWORD)sizeof(basic_info)) == TRUE);
         assert(GetLastError() == ERROR_SUCCESS);
         assert(basic_info.CreationTime.QuadPart != 0);
         assert(basic_info.LastWriteTime.QuadPart != 0);
         assert((basic_info.FileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0);

         memset(&standard_info, 0xA5, sizeof(standard_info));
         assert(GetFileInformationByHandleEx(reader, FileStandardInfo, &standard_info,
                                              (DWORD)sizeof(standard_info)) == TRUE);
         assert(standard_info.EndOfFile.QuadPart == (LONGLONG)(sizeof(payload) - 1));
         assert(standard_info.AllocationSize.QuadPart >= standard_info.EndOfFile.QuadPart);
         assert(standard_info.NumberOfLinks >= 1);
         assert(standard_info.DeletePending == FALSE && standard_info.Directory == FALSE);

         memset(&tag_info, 0xA5, sizeof(tag_info));
         assert(GetFileInformationByHandleEx(reader, FileAttributeTagInfo, &tag_info,
                                              (DWORD)sizeof(tag_info)) == TRUE);
         assert((tag_info.FileAttributes & FILE_ATTRIBUTE_ARCHIVE) != 0);
         assert(tag_info.ReparseTag == 0);

         memset(&id_info, 0xA5, sizeof(id_info));
         assert(GetFileInformationByHandleEx(reader, FileIdInfo, &id_info,
                                              (DWORD)sizeof(id_info)) == TRUE);
         assert(id_info.VolumeSerialNumber != 0 ||
                memcmp(id_info.FileId.Identifier, (BYTE[16]){0}, 16) != 0);
         assert(memcmp(id_info.FileId.Identifier + 8, (BYTE[8]){0}, 8) == 0);
         memset(&alignment_info, 0xA5, sizeof(alignment_info));
         assert(GetFileInformationByHandleEx(reader, FileAlignmentInfo, &alignment_info,
                                              (DWORD)sizeof(alignment_info)) == TRUE);
         assert(alignment_info.AlignmentRequirement == 0);

         memset(name_storage, 0xA5, sizeof(name_storage));
         assert(GetFileInformationByHandleEx(reader, FileNameInfo, name_info,
                                              (DWORD)sizeof(name_storage)) == TRUE);
         assert(GetLastError() == ERROR_SUCCESS);
         assert(name_info->FileNameLength == strlen(path) * sizeof(WORD));
         assert((name_info->FileNameLength % sizeof(WORD)) == 0);
         for (size_t i = 0; i < strlen(path); ++i)
            assert(name_info->FileName[i] == (WORD)(unsigned char)path[i]);
         memset(name_storage, 0xA5, sizeof(name_storage));
         assert(GetFileInformationByHandleEx(reader, FileNormalizedNameInfo, name_info,
                                              (DWORD)sizeof(name_storage)) == TRUE);
         assert(name_info->FileNameLength == strlen(path) * sizeof(WORD));
         for (size_t i = 0; i < strlen(path); ++i)
            assert(name_info->FileName[i] == (WORD)(unsigned char)path[i]);
         memset(name_storage, 0xA5, sizeof(name_storage));
         assert(GetFileInformationByHandleEx(reader, FileNameInfo, name_info,
                                              (DWORD)(offsetof(FILE_NAME_INFO, FileName) +
                                                      (strlen(path) - 1) * sizeof(WORD))) == FALSE);
         assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
         for (size_t i = 0; i < sizeof(name_storage); ++i) assert(name_storage[i] == 0xA5);

         memset(short_buffer, 0xA5, sizeof(short_buffer));
         assert(GetFileInformationByHandleEx(reader, FileBasicInfo, short_buffer,
                                              (DWORD)(sizeof(FILE_BASIC_INFO) - 1)) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         for (size_t i = 0; i < sizeof(short_buffer); ++i) assert(short_buffer[i] == 0xA5);
         memset(&id_info, 0xA5, sizeof(id_info));
         assert(GetFileInformationByHandleEx(reader, FileIdInfo, &id_info,
                                              (DWORD)(sizeof(id_info) - 1)) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         for (size_t i = 0; i < sizeof(id_info); ++i)
            assert(((BYTE *)&id_info)[i] == 0xA5);
         memset(&alignment_info, 0xA5, sizeof(alignment_info));
         assert(GetFileInformationByHandleEx(reader, FileAlignmentInfo, &alignment_info,
                                              (DWORD)(sizeof(alignment_info) - 1)) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(alignment_info.AlignmentRequirement == 0xA5A5A5A5u);
         preserved_basic = basic_info;
         assert(GetFileInformationByHandleEx(reader, (FILE_INFO_BY_HANDLE_CLASS)999,
                                              &basic_info, (DWORD)sizeof(basic_info)) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(memcmp(&basic_info, &preserved_basic, sizeof(basic_info)) == 0);
         assert(GetFileInformationByHandleEx(reader, FileBasicInfo, NULL,
                                              (DWORD)sizeof(basic_info)) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(GetFileInformationByHandleEx((HANDLE)(uintptr_t)0x1234, FileBasicInfo,
                                              &basic_info, (DWORD)sizeof(basic_info)) == FALSE);
         assert(GetLastError() == ERROR_INVALID_HANDLE);
         assert(memcmp(&basic_info, &preserved_basic, sizeof(basic_info)) == 0);
         {
            FILE_END_OF_FILE_INFO eof_info;
            eof_info.EndOfFile.QuadPart = 1;
            assert(SetFileInformationByHandle(reader, FileEndOfFileInfo, &eof_info,
                                               (DWORD)sizeof(eof_info)) == FALSE);
            assert(GetLastError() == ERROR_ACCESS_DENIED);
         }
      }
      /* Byte-range locks are tracked per handle and must be explicitly
       * unlocked with the same range. Linux OFD locks make independent opens
       * conflict even when they originate in this process. */
      {
         HANDLE lock_a = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
         HANDLE lock_b = CreateFile(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
                                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
         assert(lock_a != INVALID_HANDLE_VALUE && lock_b != INVALID_HANDLE_VALUE);
         assert(UnlockFile(lock_a, 0, 0, 1, 0) == FALSE);
         assert(GetLastError() == ERROR_NOT_LOCKED);
         assert(LockFile(lock_a, 0, 0, 16, 0) == TRUE);
         assert(LockFile(lock_a, 8, 0, 8, 0) == FALSE);
         assert(GetLastError() == ERROR_LOCK_VIOLATION);
#ifdef F_OFD_SETLK
         assert(LockFile(lock_b, 0, 0, 16, 0) == FALSE);
         assert(GetLastError() == ERROR_LOCK_VIOLATION);
#endif
         assert(UnlockFile(lock_a, 0, 0, 8, 0) == FALSE);
         assert(GetLastError() == ERROR_NOT_LOCKED);
         assert(UnlockFile(lock_a, 0, 0, 16, 0) == TRUE);
         assert(UnlockFile(lock_a, 0, 0, 16, 0) == FALSE);
         assert(GetLastError() == ERROR_NOT_LOCKED);
         assert(LockFile(lock_b, 0, 0, 16, 0) == TRUE);
         assert(CloseHandle(lock_b) == TRUE);
         /* Closing a handle releases its kernel byte-range locks. */
         assert(LockFile(lock_a, 0, 0, 16, 0) == TRUE);
         assert(UnlockFile(lock_a, 0, 0, 16, 0) == TRUE);
         assert(UnlockFile(lock_a, 0, 0, 16, 0) == FALSE);
         assert(GetLastError() == ERROR_NOT_LOCKED);
         assert(CloseHandle(lock_a) == TRUE);
         assert(LockFile(writer, 0, 0, 0, 0) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(LockFile(writer, 0xffffffffu, 0x7fffffffu, 2, 0) == FALSE);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
      }
      transferred = 99;
      /* Synchronous file EOF is a successful zero-byte read. */
      assert(ReadFile(reader, actual, 1, &transferred, NULL) == TRUE);
      assert(transferred == 0);
      assert(WriteFile(reader, payload, 1, &transferred, NULL) == FALSE);
      assert(GetLastError() == ERROR_ACCESS_DENIED);
      assert(FlushFileBuffers(reader) == FALSE);
      assert(GetLastError() == ERROR_ACCESS_DENIED);
      assert(SetFileTime(reader, NULL, NULL, &(FILETIME){0x12345678u, 0x01D6E3A1u}) == FALSE);
      assert(GetLastError() == ERROR_ACCESS_DENIED);
      assert(SetEndOfFile(reader) == FALSE);
      assert(GetLastError() == ERROR_ACCESS_DENIED);
      assert(ReadFile(reader, actual, 1, NULL, NULL) == FALSE);
      assert(GetLastError() == ERROR_INVALID_PARAMETER);
      assert(ReadFile(reader, actual, 1, &transferred, (LPOVERLAPPED)(uintptr_t)1) == FALSE);
      assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
      assert(CloseHandle(reader) == TRUE);
      assert(GetFileSizeEx(reader, &(LARGE_INTEGER){0}) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(GetFileType(reader) == FILE_TYPE_UNKNOWN);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(LockFile(reader, 0, 0, 1, 0) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(UnlockFile(reader, 0, 0, 1, 0) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(GetFileInformationByHandle(reader, &(BY_HANDLE_FILE_INFORMATION){0}) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(FlushFileBuffers(reader) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(SetEndOfFile(reader) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(ReadFile(reader, actual, 1, &transferred, NULL) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(unlink(path) == 0);
   }
   {
      char unicode_path[] = "/tmp/winapi-name-\xC3\xA8-XXXXXX";
      int raw = mkstemp(unicode_path);
      HANDLE unicode_handle;
      unsigned char storage[1024];
      FILE_NAME_INFO *info = (FILE_NAME_INFO *)storage;
      assert(raw >= 0);
      assert(close(raw) == 0);
      unicode_handle = CreateFile(unicode_path, GENERIC_READ, 0, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(unicode_handle != INVALID_HANDLE_VALUE && unicode_handle != NULL);
      memset(storage, 0xA5, sizeof(storage));
      assert(GetFileInformationByHandleEx(unicode_handle, FileNameInfo, info,
                                           (DWORD)sizeof(storage)) == TRUE);
      assert(info->FileNameLength == (strlen("/tmp/winapi-name-") + 1 + 7) * sizeof(WORD));
      assert(info->FileName[strlen("/tmp/winapi-name-")] == 0x00E8u);
      assert(unlink(unicode_path) == 0);
      memset(storage, 0xA5, sizeof(storage));
      assert(GetFileInformationByHandleEx(unicode_handle, FileNameInfo, info,
                                           (DWORD)sizeof(storage)) == FALSE);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      for (size_t i = 0; i < sizeof(storage); ++i) assert(storage[i] == 0xA5);
      assert(CloseHandle(unicode_handle) == TRUE);
   }
   {
      /* POSIX permits byte sequences that are not UTF-8; do not silently
       * replace them when exposing a Win32 UTF-16 file name. */
      char invalid_utf8_path[] = "/tmp/winapi-name-\xFF-XXXXXX";
      int raw = mkstemp(invalid_utf8_path);
      HANDLE invalid_utf8_handle;
      unsigned char storage[256];
      FILE_NAME_INFO *info = (FILE_NAME_INFO *)storage;
      assert(raw >= 0);
      assert(close(raw) == 0);
      invalid_utf8_handle = CreateFile(invalid_utf8_path, GENERIC_READ, 0, NULL,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      assert(invalid_utf8_handle != INVALID_HANDLE_VALUE && invalid_utf8_handle != NULL);
      memset(storage, 0xA5, sizeof(storage));
      assert(GetFileInformationByHandleEx(invalid_utf8_handle, FileNameInfo, info,
                                           (DWORD)sizeof(storage)) == FALSE);
      assert(GetLastError() == ERROR_NO_UNICODE_TRANSLATION);
      for (size_t i = 0; i < sizeof(storage); ++i) assert(storage[i] == 0xA5);
      assert(CloseHandle(invalid_utf8_handle) == TRUE);
      assert(unlink(invalid_utf8_path) == 0);
   }
   /* Win32-style path mutation APIs: don't silently overwrite destinations,
    * don't delete directories through DeleteFile, and honor read-only files. */
   {
      char directory[] = "/tmp/winapi-fsops-XXXXXX";
      char source[256], destination[256], copy_path[256], nested[256], moved_dir[256], renamed_dir[256];
      char wt_src_dir[256], wt_dst_dir[256], wt_src[256], wt_dst[256];
      int raw;
      DWORD sectors_per_cluster = 0, bytes_per_sector = 0;
      DWORD free_clusters = 0, total_clusters = 0;
      assert(mkdtemp(directory) != NULL);
      assert(snprintf(source, sizeof(source), "%s/source.txt", directory) > 0);
      assert(snprintf(destination, sizeof(destination), "%s/destination.txt", directory) > 0);
      assert(snprintf(copy_path, sizeof(copy_path), "%s/copied.txt", directory) > 0);
      assert(snprintf(nested, sizeof(nested), "%s/nested", directory) > 0);
      assert(snprintf(moved_dir, sizeof(moved_dir), "%s/moved-dir", directory) > 0);
      assert(snprintf(renamed_dir, sizeof(renamed_dir), "%s/renamed-dir", directory) > 0);
      assert(snprintf(wt_src_dir, sizeof(wt_src_dir), "%s/write-through-src", directory) > 0);
      assert(snprintf(wt_dst_dir, sizeof(wt_dst_dir), "%s/write-through-dst", directory) > 0);
      assert(snprintf(wt_src, sizeof(wt_src), "%s/payload.bin", wt_src_dir) > 0);
      assert(snprintf(wt_dst, sizeof(wt_dst), "%s/payload.bin", wt_dst_dir) > 0);
      raw = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600);
      assert(raw >= 0);
      assert(write(raw, "abc", 3) == 3);
      assert(close(raw) == 0);
      assert(GetFileAttributes(source) != INVALID_FILE_ATTRIBUTES);
      assert(SetFileAttributes(source, FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_ARCHIVE) == TRUE);
      assert((GetFileAttributes(source) & FILE_ATTRIBUTE_READONLY) != 0);
      assert(DeleteFile(source) == FALSE && GetLastError() == ERROR_ACCESS_DENIED);
      assert(SetFileAttributes(source, FILE_ATTRIBUTE_ARCHIVE) == TRUE);
      /* ARCHIVE is a synthesized POSIX projection, not a mutable inode flag.
       * Requests that would clear it must fail rather than report false success. */
      assert(SetFileAttributes(source, FILE_ATTRIBUTE_NORMAL) == FALSE);
      assert(GetLastError() == ERROR_NOT_SUPPORTED);
      assert((GetFileAttributes(source) & FILE_ATTRIBUTE_ARCHIVE) != 0);
      /* CopyFile publishes only complete copies, honors no-overwrite, and can
       * atomically replace an existing destination when requested. */
      assert(CopyFile(source, copy_path, TRUE) == TRUE);
      {
         char copied[8] = {0};
         int copied_fd = open(copy_path, O_RDONLY);
         assert(copied_fd >= 0);
         assert(read(copied_fd, copied, sizeof(copied)) == 3);
         assert(close(copied_fd) == 0);
         assert(memcmp(copied, "abc", 3) == 0);
      }
      assert(CopyFile(source, copy_path, TRUE) == FALSE);
      assert(GetLastError() == ERROR_ALREADY_EXISTS);
      raw = open(source, O_WRONLY | O_TRUNC);
      assert(raw >= 0 && write(raw, "xyz12", 5) == 5 && close(raw) == 0);
      assert(CopyFile(source, copy_path, FALSE) == TRUE);
      {
         char copied[8] = {0};
         int copied_fd = open(copy_path, O_RDONLY);
         assert(copied_fd >= 0);
         assert(read(copied_fd, copied, sizeof(copied)) == 5);
         assert(close(copied_fd) == 0);
         assert(memcmp(copied, "xyz12", 5) == 0);
      }
      assert(CopyFile(source, source, FALSE) == FALSE);
      assert(CopyFile(directory, copy_path, FALSE) == FALSE);
      assert(CopyFile("/definitely/not/a/babyc-file", copy_path, FALSE) == FALSE);
      assert(DeleteFile(copy_path) == TRUE);
      {
         char large_source[256], large_copy[256];
         const size_t payload_size = 160123;
         unsigned char *payload = (unsigned char *)malloc(payload_size);
         unsigned char *roundtrip = (unsigned char *)malloc(payload_size);
         size_t offset = 0;
         int in_fd, out_fd;
         assert(payload != NULL && roundtrip != NULL);
         assert(snprintf(large_source, sizeof(large_source), "%s/large.bin", directory) > 0);
         assert(snprintf(large_copy, sizeof(large_copy), "%s/large-copy.bin", directory) > 0);
         for (size_t i = 0; i < payload_size; ++i) payload[i] = (unsigned char)((i * 37u + 11u) & 0xffu);
         in_fd = open(large_source, O_CREAT | O_EXCL | O_WRONLY, 0600);
         assert(in_fd >= 0 && write(in_fd, payload, payload_size) == (ssize_t)payload_size);
         assert(close(in_fd) == 0);
         assert(CopyFile(large_source, large_copy, TRUE) == TRUE);
         out_fd = open(large_copy, O_RDONLY);
         assert(out_fd >= 0);
         while (offset < payload_size) {
            ssize_t got = read(out_fd, roundtrip + offset, payload_size - offset);
            assert(got > 0);
            offset += (size_t)got;
         }
         assert(close(out_fd) == 0);
         assert(memcmp(payload, roundtrip, payload_size) == 0);
         assert(DeleteFile(large_source) == TRUE && DeleteFile(large_copy) == TRUE);
         free(payload); free(roundtrip);
      }
      assert(MoveFile(source, destination) == TRUE);
      assert(access(source, F_OK) != 0 && access(destination, F_OK) == 0);
      raw = open(source, O_CREAT | O_EXCL | O_WRONLY, 0600);
      assert(raw >= 0 && close(raw) == 0);
      assert(MoveFile(source, destination) == FALSE);
      assert(GetLastError() == ERROR_ALREADY_EXISTS);
      assert(MoveFileEx(source, destination, 0x80000000u) == FALSE);
      assert(GetLastError() == ERROR_INVALID_PARAMETER);
      assert(MoveFileEx(source, destination, MOVEFILE_DELAY_UNTIL_REBOOT) == FALSE);
      assert(GetLastError() == ERROR_NOT_SUPPORTED);
      assert(MoveFileEx(source, destination, 0) == FALSE);
      assert(GetLastError() == ERROR_ALREADY_EXISTS);
      assert(MoveFileEx(source, destination,
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == TRUE);
      assert(access(source, F_OK) != 0 && access(destination, F_OK) == 0);
      assert(DeleteFile(destination) == TRUE);
      assert(DeleteFile(directory) == FALSE && GetLastError() == ERROR_ACCESS_DENIED);
      assert(CreateDirectory(nested, NULL) == TRUE);
      assert((GetFileAttributes(nested) & FILE_ATTRIBUTE_DIRECTORY) != 0);
      {
         WIN32_FILE_ATTRIBUTE_DATA directory_data;
         assert(GetFileAttributesEx(nested, GetFileExInfoStandard, &directory_data) == TRUE);
         assert((directory_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
         assert(directory_data.nFileSizeHigh == 0 && directory_data.nFileSizeLow == 0);
      }
      assert(CreateDirectory(nested, NULL) == FALSE && GetLastError() == ERROR_ALREADY_EXISTS);
      assert(RemoveDirectory(nested) == TRUE);
      assert(CreateDirectory(moved_dir, NULL) == TRUE);
      assert(MoveFile(moved_dir, renamed_dir) == TRUE);
      assert(RemoveDirectory(moved_dir) == FALSE);
      assert(RemoveDirectory(renamed_dir) == TRUE);
      /* WRITE_THROUGH must sync both directory entries when a rename crosses
       * parent directories, not only the destination directory. */
      assert(CreateDirectory(wt_src_dir, NULL) == TRUE);
      assert(CreateDirectory(wt_dst_dir, NULL) == TRUE);
      raw = open(wt_src, O_CREAT | O_EXCL | O_WRONLY, 0600);
      assert(raw >= 0 && write(raw, "durable", 7) == 7 && close(raw) == 0);
      assert(MoveFileEx(wt_src, wt_dst, MOVEFILE_WRITE_THROUGH) == TRUE);
      assert(access(wt_src, F_OK) != 0 && access(wt_dst, F_OK) == 0);
      assert(DeleteFile(wt_dst) == TRUE);
      assert(RemoveDirectory(wt_src_dir) == TRUE);
      assert(RemoveDirectory(wt_dst_dir) == TRUE);
      /* Exercise MOVEFILE_COPY_ALLOWED across filesystems when /dev/shm is a
       * distinct mounted filesystem on this runner. */
      {
         struct stat tmp_st, shm_st;
         if (stat(directory, &tmp_st) == 0 && stat("/dev/shm", &shm_st) == 0 &&
             S_ISDIR(shm_st.st_mode) && tmp_st.st_dev != shm_st.st_dev) {
            char cross_source[256], cross_target[256];
            assert(snprintf(cross_source, sizeof(cross_source), "%s/cross-volume.txt", directory) > 0);
            assert(snprintf(cross_target, sizeof(cross_target), "/dev/shm/babyc-moveex-%ld.tmp", (long)getpid()) > 0);
            assert(access(cross_target, F_OK) != 0);
            raw = open(cross_source, O_CREAT | O_EXCL | O_WRONLY, 0600);
            assert(raw >= 0 && write(raw, "cross-volume", 12) == 12 && close(raw) == 0);
            assert(MoveFileEx(cross_source, cross_target, MOVEFILE_COPY_ALLOWED) == TRUE);
            assert(access(cross_source, F_OK) != 0);
            raw = open(cross_target, O_RDONLY);
            assert(raw >= 0);
            { char cross_data[16] = {0}; assert(read(raw, cross_data, sizeof(cross_data)) == 12); assert(memcmp(cross_data, "cross-volume", 12) == 0); }
            assert(close(raw) == 0 && DeleteFile(cross_target) == TRUE);
         }
      }
      assert(RemoveDirectory(directory) == TRUE);
      assert(DeleteFile("/definitely/not/a/babyc-file") == FALSE);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      assert(MoveFile("/definitely/not/a/babyc-file", destination) == FALSE);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      assert(SetFileAttributes("/definitely/not/a/babyc-file", FILE_ATTRIBUTE_ARCHIVE) == FALSE);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      assert(RemoveDirectory("/definitely/not/a/babyc-directory") == FALSE);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      assert(CreateDirectory(directory, (LPSECURITY_ATTRIBUTES)(uintptr_t)1) == FALSE);
      assert(GetLastError() == ERROR_NOT_SUPPORTED);
      {
         char temp_dir[MAX_PATH], temp_name[MAX_PATH], predicted[MAX_PATH];
         char too_small[2] = {'x', 'x'};
         DWORD temp_len = GetTempPath((DWORD)sizeof(temp_dir), temp_dir);
         assert(temp_len > 0 && temp_len < sizeof(temp_dir));
         assert(temp_dir[temp_len - 1] == '/');
         assert(GetTempPath((DWORD)sizeof(too_small), too_small) == temp_len + 1);
         assert(too_small[0] == '\0' && GetLastError() == ERROR_BUFFER_OVERFLOW);
         assert(GetTempPath(0, NULL) == temp_len + 1);
         assert(GetTempFileName("/tmp", "bct", 0, temp_name) != 0);
         assert(access(temp_name, F_OK) == 0);
         assert(DeleteFile(temp_name) == TRUE);
         /* Prefix is one filename component; it cannot escape temp_dir. */
         assert(GetTempFileName("/tmp", "../", 0, temp_name) == 0);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(GetTempFileName("/tmp", "a\\b", 0, temp_name) == 0);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
         assert(GetTempFileName("/tmp", "bct", 0x1234u, predicted) == 0x1234u);
         assert(access(predicted, F_OK) != 0);
         assert(GetTempFileName("/definitely/not/a/directory", "bct", 0, temp_name) == 0);
         assert(GetTempFileName(directory, "bct", 0, NULL) == 0);
         assert(GetLastError() == ERROR_INVALID_PARAMETER);
      }
      assert(GetDiskFreeSpace("/tmp", &sectors_per_cluster, &bytes_per_sector,
                              &free_clusters, &total_clusters) == TRUE);
      assert(sectors_per_cluster > 0 && bytes_per_sector == 512);
      assert(total_clusters > 0 && free_clusters <= total_clusters);
      assert(GetDiskFreeSpace("/definitely/not/a/directory", &sectors_per_cluster,
                              &bytes_per_sector, &free_clusters, &total_clusters) == FALSE);
      assert(GetDiskFreeSpace("/tmp", NULL, &bytes_per_sector,
                              &free_clusters, &total_clusters) == FALSE);
      assert(GetLastError() == ERROR_INVALID_PARAMETER);
   }
   /* Closing a file handle while another thread is repeatedly seeking and
      reading must drain operation leases before destroying the entry/lock. */
   {
      char path[] = "/tmp/winapi-close-race-XXXXXX";
      int raw = mkstemp(path);
      assert(raw >= 0);
      assert(write(raw, "x", 1) == 1);
      assert(close(raw) == 0);
      HANDLE racing = CreateFile(path, GENERIC_READ, 0, NULL, OPEN_EXISTING,
                                 FILE_ATTRIBUTE_NORMAL, NULL);
      assert(racing != INVALID_HANDLE_VALUE && racing != NULL);
      file_close_race_args args;
      args.handle = racing;
      atomic_init(&args.successful_reads, 0);
      /* Several operations can be queued on the same per-handle I/O lock.
       * Closing must reject new leases and drain every lease already admitted. */
      pthread_t worker_threads[4];
      for (unsigned int i = 0; i < 4; ++i)
         assert(pthread_create(&worker_threads[i], NULL, file_close_race_worker, &args) == 0);
      while (atomic_load_explicit(&args.successful_reads, memory_order_relaxed) < 100)
         sched_yield();
      assert(CloseHandle(racing) == TRUE);
      for (unsigned int i = 0; i < 4; ++i)
         assert(pthread_join(worker_threads[i], NULL) == 0);
      assert(atomic_load_explicit(&args.successful_reads, memory_order_relaxed) >= 100);
      assert(CloseHandle(racing) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(unlink(path) == 0);
   }
   {
      pthread_t workers[4];
      file_handle_stress_args args[4];
      for (unsigned int i = 0; i < 4; ++i) {
         args[i].worker_id = i; args[i].iterations = 50;
         assert(pthread_create(&workers[i], NULL, file_handle_stress_worker, &args[i]) == 0);
      }
      for (unsigned int i = 0; i < 4; ++i) assert(pthread_join(workers[i], NULL) == 0);
   }
#if defined(ENABLE_WSHMEM) && ENABLE_WSHMEM
   /* Simulate a creator dying after publishing an incomplete named object.
      This deliberately uses the private lock-record layout to exercise the
      crash-recovery protocol without a timing-dependent kill race. */
   if ((argc == 3 || argc == 4) && strcmp(argv[1], "--shmem-stale-init-child") == 0) {
      struct test_shm_lock_record { uint64_t magic; char name[MAX_PATH + 1]; unsigned char state; } record;
      uint64_t hash = UINT64_C(1469598103934665603);
      uint64_t hash_b = UINT64_C(0x9e3779b97f4a7c15);
      const unsigned char *np = (const unsigned char *)argv[2];
      char lock_dir[96], lock_path[256], posix_name[MAX_PATH + 2];
      int lock_fd, shm_fd;
      while (*np != 0) {
         hash ^= *np;
         hash *= UINT64_C(1099511628211);
         hash_b ^= (uint64_t)*np + UINT64_C(0x9e3779b97f4a7c15) + (hash_b << 6) + (hash_b >> 2);
         ++np;
      }
      snprintf(lock_dir, sizeof(lock_dir), "/tmp/winapi-shm-locks-%lu", (unsigned long)getuid());
      if (mkdir(lock_dir, S_IRWXU) != 0 && errno != EEXIST) _exit(50);
      snprintf(lock_path, sizeof(lock_path), "%s/lock-%016llx-%016llx", lock_dir,
         (unsigned long long)hash, (unsigned long long)hash_b);
      lock_fd = open(lock_path, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
      if (lock_fd < 0 || flock(lock_fd, LOCK_EX) != 0) _exit(51);
      memset(&record, 0, sizeof(record));
      record.magic = UINT64_C(0x57494e53484d3031);
      snprintf(record.name, sizeof(record.name), "%s", argv[2]);
      record.state = 1u;
      if (ftruncate(lock_fd, (off_t)sizeof(record)) != 0 ||
          pwrite(lock_fd, &record, sizeof(record), 0) != (ssize_t)sizeof(record)) _exit(52);
      if (argc == 4 && strcmp(argv[3], "no-object") == 0)
         _exit(0); /* Simulate death after INITIALIZING but before shm_open. */
      posix_name[0] = '/';
      snprintf(posix_name + 1, sizeof(posix_name) - 1, "%s", argv[2]);
      shm_fd = shm_open(posix_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
      if (shm_fd < 0) _exit(53);
      /* Keep the object at size zero to represent death before ftruncate. */
      _exit(0); /* Kernel closes descriptors and releases flock. */
   }
   /* Dedicated exec-child mode: deliberately exit with a live mapping and
      view, letting the kernel reclaim process-local descriptors and mappings. */
   if (argc == 3 && strcmp(argv[1], "--shmem-crash-child") == 0) {
      HANDLE child_mapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, argv[2]);
      if (child_mapping == NULL) _exit(31);
      unsigned char *child_view = (unsigned char *)MapViewOfFile(child_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      if (child_view == NULL) _exit(32);
      child_view[18] = 0xD8;
      _exit(0); /* Intentionally omit UnmapViewOfFile and CloseHandle. */
   }
   if (argc == 3 && strcmp(argv[1], "--shmem-create-race-child") == 0) {
      HANDLE child_mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
         PAGE_READWRITE, 0, 4096, argv[2]);
      if (child_mapping == NULL) _exit(41);
      DWORD creation_error = GetLastError();
      if (creation_error != ERROR_SUCCESS && creation_error != ERROR_ALREADY_EXISTS) _exit(42);
      unsigned char *child_view = (unsigned char *)MapViewOfFile(child_mapping,
         FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      if (child_view == NULL) _exit(43);
      if (!UnmapViewOfFile(child_view)) _exit(44);
      if (!CloseHandle(child_mapping)) _exit(45);
      _exit(0);
   }
#endif
#if defined(ENABLE_WTHREADS) && ENABLE_WTHREADS
   /* Gli handle opachi non validi devono essere rifiutati senza dereferenziare
      il valore fornito dal chiamante. */
   assert(SetEvent((HANDLE)(uintptr_t)0x1234) == FALSE);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(ResetEvent((HANDLE)(uintptr_t)0x1234) == FALSE);
   assert(ReleaseMutex((HANDLE)(uintptr_t)0x1234) == FALSE);
   DWORD invalid_exit_code = 0;
   assert(GetExitCodeThread((HANDLE)(uintptr_t)0x1234, &invalid_exit_code) == FALSE);
   assert(GetExitCodeThread(NULL, NULL) == FALSE);
   assert(PulseEvent((HANDLE)(uintptr_t)0x1234) == FALSE);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   assert(CreateEvent(NULL, TRUE, FALSE, "winapi-test-named-event") == NULL);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   assert(OpenEvent(EVENT_ALL_ACCESS, FALSE, "winapi-test-named-event") == NULL);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   assert(CreateMutex(NULL, FALSE, "winapi-test-named-mutex") == NULL);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   assert(SuspendThread((HANDLE)(uintptr_t)0x1234) == (DWORD)-1);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   assert(CreateThread(NULL, 0, signal_event_worker, NULL, CREATE_SUSPENDED, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(CreateThread(NULL, 0, NULL, NULL, 0, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(CreateThread(NULL, 1, return_zero, NULL, 0, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   HANDLE invalid_priority_thread = (HANDLE)(uintptr_t)0x1234;
   assert(SetThreadPriority(invalid_priority_thread, THREAD_PRIORITY_NORMAL) == FALSE);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(SetThreadPriority(invalid_priority_thread, 99) == FALSE);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);

   HANDLE test_event = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(test_event != NULL && test_event != INVALID_HANDLE_VALUE);
   assert(SetEvent(test_event) == TRUE);
   assert(WaitForSingleObject(test_event, 0) == WAIT_OBJECT_0);
   assert(ResetEvent(test_event) == TRUE);
   assert(WaitForSingleObject(test_event, 0) == WAIT_TIMEOUT);
   assert(CloseHandle(test_event) == TRUE);
   assert(CloseHandle(test_event) == FALSE);
   /* A closed handle must not become valid again when a later object is
    * allocated at the same address (stale-handle/ABA regression). */
   HANDLE stale_event = test_event;
   HANDLE replacement_event = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(replacement_event != NULL && replacement_event != stale_event);
   assert(SetEvent(stale_event) == FALSE);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(CloseHandle(replacement_event) == TRUE);

   /* WaitForMultipleObjects validates its input before allocating resources. */
   assert(WaitForMultipleObjects(0, NULL, FALSE, 0) == WAIT_FAILED);
   HANDLE invalid_waits[1] = {(HANDLE)(uintptr_t)0x1234};
   assert(WaitForMultipleObjects(1, invalid_waits, FALSE, 0) == WAIT_FAILED);
   HANDLE event_a = CreateEvent(NULL, TRUE, TRUE, NULL);
   HANDLE event_b = CreateEvent(NULL, TRUE, TRUE, NULL);
   assert(event_a != NULL && event_b != NULL);
   HANDLE wait_all_handles[2] = {event_a, event_b};
   assert(WaitForMultipleObjects(2, wait_all_handles, TRUE, 0) == WAIT_OBJECT_0);
   assert(ResetEvent(event_b) == TRUE);
   assert(WaitForMultipleObjects(2, wait_all_handles, TRUE, 0) == WAIT_TIMEOUT);
   assert(WaitForMultipleObjects(2, wait_all_handles, FALSE, 5) == WAIT_OBJECT_0);
   assert(SetEvent(event_b) == TRUE);
   assert(WaitForMultipleObjects(2, wait_all_handles, FALSE, 0) == WAIT_OBJECT_0);
   assert(CloseHandle(event_a) == TRUE);
   assert(CloseHandle(event_b) == TRUE);

   HANDLE all_a = CreateEvent(NULL, TRUE, TRUE, NULL);
   HANDLE all_b = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(all_a != NULL && all_b != NULL);
   wait_test_args all_signal_args = { all_b, WAIT_FAILED };
   HANDLE all_signaler = CreateThread(NULL, 0, signal_event_worker, &all_signal_args, 0, NULL);
   assert(all_signaler != NULL);
   HANDLE all_waits[2] = { all_a, all_b };
   assert(WaitForMultipleObjects(2, all_waits, TRUE, 1000) == WAIT_OBJECT_0);
   assert(WaitForSingleObject(all_signaler, 1000) == WAIT_OBJECT_0);
   assert(CloseHandle(all_signaler) == TRUE);
   assert(CloseHandle(all_a) == TRUE); assert(CloseHandle(all_b) == TRUE);

   HANDLE auto_event = CreateEvent(NULL, FALSE, TRUE, NULL);
   assert(auto_event != NULL);
   assert(WaitForSingleObject(auto_event, 0) == WAIT_OBJECT_0);
   assert(WaitForSingleObject(auto_event, 0) == WAIT_TIMEOUT);
   assert(CloseHandle(auto_event) == TRUE);

   /* WaitForMultipleObjects deve dormire su signaling, senza polling: un altro
    * thread segnala l'evento dopo che l'attesa è iniziata. */
   HANDLE signaled_later = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(signaled_later != NULL);
   wait_test_args signal_args = { signaled_later, WAIT_FAILED };
   HANDLE signal_thread = CreateThread(NULL, 0, signal_event_worker, &signal_args, 0, NULL);
   assert(signal_thread != NULL);
   HANDLE signal_waits[1] = { signaled_later };
   assert(WaitForMultipleObjects(1, signal_waits, FALSE, 1000) == WAIT_OBJECT_0);
   assert(WaitForSingleObject(signal_thread, 1000) == WAIT_OBJECT_0);
   DWORD signal_exit = 0; assert(GetExitCodeThread(signal_thread, &signal_exit) == TRUE);
   assert(signal_exit == 23);
   assert(CloseHandle(signal_thread) == TRUE);
   assert(CloseHandle(signaled_later) == TRUE);

   /* DWORD thread exit values must survive without signed-int truncation. */
   HANDLE high_exit_thread = CreateThread(NULL, 0, return_high_exit_code, NULL, 0, NULL);
   assert(high_exit_thread != NULL);
   assert(WaitForSingleObject(high_exit_thread, 1000) == WAIT_OBJECT_0);
   DWORD high_exit = 0;
   assert(GetExitCodeThread(high_exit_thread, &high_exit) == TRUE);
   assert(high_exit == (DWORD)0xF1234567u);
   assert(CloseHandle(high_exit_thread) == TRUE);

   /* Duplicate handles are invalid for WaitForMultipleObjects. */
   HANDLE duplicate_waits[2] = { event_a, event_a };
   HANDLE duplicate_event = CreateEvent(NULL, TRUE, TRUE, NULL);
   assert(duplicate_event != NULL);
   duplicate_waits[0] = duplicate_event; duplicate_waits[1] = duplicate_event;
   assert(WaitForMultipleObjects(2, duplicate_waits, FALSE, 0) == WAIT_FAILED);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(CloseHandle(duplicate_event) == TRUE);

   /* Un handle non può essere distrutto mentre una wait lo ha pinzato. */
   HANDLE pinned_event = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(pinned_event != NULL);
   wait_test_args wait_args = { pinned_event, WAIT_FAILED };
   HANDLE waiter_thread = CreateThread(NULL, 0, wait_event_worker, &wait_args, 0, NULL);
   assert(waiter_thread != NULL);
   DWORD running_exit = 0;
   assert(GetExitCodeThread(waiter_thread, &running_exit) == TRUE);
   assert(running_exit == STILL_ACTIVE);
   assert(CloseHandle(waiter_thread) == FALSE);
   assert(GetLastError() == ERROR_BUSY);
   Sleep(20);
   assert(CloseHandle(pinned_event) == FALSE);
   assert(GetLastError() == ERROR_BUSY);
   assert(SetEvent(pinned_event) == TRUE);
   assert(WaitForSingleObject(waiter_thread, 1000) == WAIT_OBJECT_0);
   assert(wait_args.result == WAIT_OBJECT_0);
   assert(CloseHandle(waiter_thread) == TRUE);
   assert(CloseHandle(pinned_event) == TRUE);

   HANDLE test_mutex = CreateMutex(NULL, TRUE, NULL);
   assert(test_mutex != NULL && test_mutex != INVALID_HANDLE_VALUE);
   /* Non distruggere un mutex POSIX ancora posseduto: il handle resta valido. */
   assert(CloseHandle(test_mutex) == FALSE);
   assert(ReleaseMutex(test_mutex) == TRUE);
   assert(ReleaseMutex(test_mutex) == FALSE);
   assert(CloseHandle(test_mutex) == TRUE);

   HANDLE abandoned_mutex = CreateMutex(NULL, FALSE, NULL);
   assert(abandoned_mutex != NULL);
   HANDLE abandoner = CreateThread(NULL, 0, abandon_mutex_worker, abandoned_mutex, 0, NULL);
   assert(abandoner != NULL);
   assert(WaitForSingleObject(abandoner, 1000) == WAIT_OBJECT_0);
   assert(CloseHandle(abandoner) == TRUE);
   /* Wait-all reports the index of the lowest abandoned mutex, not always 0. */
   HANDLE abandoned_event = CreateEvent(NULL, TRUE, TRUE, NULL);
   assert(abandoned_event != NULL);
   HANDLE abandoned_waits[2] = { abandoned_event, abandoned_mutex };
   assert(WaitForMultipleObjects(2, abandoned_waits, TRUE, 0) == WAIT_ABANDONED_0 + 1);
   assert(ReleaseMutex(abandoned_mutex) == TRUE);
   assert(CloseHandle(abandoned_event) == TRUE);
   assert(CloseHandle(abandoned_mutex) == TRUE);

   DWORD child_id = 0;
   HANDLE id_event = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(id_event != NULL);
   wait_test_args id_args = { id_event, WAIT_FAILED };
   HANDLE id_thread = CreateThread(NULL, 0, signal_event_worker, &id_args, 0, &child_id);
   /* The ID is a DWORD process-local identifier, not a truncated pthread_t. */
   assert(id_thread != NULL && child_id != 0 && child_id != GetCurrentThreadId());
   /* Test sui 7 livelli di priorità */
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_IDLE) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_IDLE);
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_LOWEST) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_LOWEST);
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_BELOW_NORMAL) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_BELOW_NORMAL);
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_NORMAL) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_NORMAL);
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_ABOVE_NORMAL) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_ABOVE_NORMAL);
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_HIGHEST) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_HIGHEST);
   assert(SetThreadPriority(id_thread, THREAD_PRIORITY_TIME_CRITICAL) == TRUE);
   assert(GetThreadPriority(id_thread) == THREAD_PRIORITY_TIME_CRITICAL);

   /* Test valore non valido */
   assert(SetThreadPriority(id_thread, 99) == FALSE);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);

   /* Test handle non valido per Set e Get */
   HANDLE invalid = (HANDLE)0xDEADBEEF;
   assert(SetThreadPriority(invalid, THREAD_PRIORITY_NORMAL) == FALSE);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(GetThreadPriority(invalid) == THREAD_PRIORITY_ERROR_RETURN);
   assert(WaitForSingleObject(id_thread, 1000) == WAIT_OBJECT_0);
   assert(CloseHandle(id_thread) == TRUE);
   assert(CloseHandle(id_event) == TRUE);

   /* Broadcast signaling wakes multiple waiters on a manual-reset event. */
   enum { WAITER_COUNT = 8 };
   HANDLE broadcast_event = CreateEvent(NULL, TRUE, FALSE, NULL);
   assert(broadcast_event != NULL);
   HANDLE waiter_handles[WAITER_COUNT];
   wait_test_args waiter_args[WAITER_COUNT];
   for (int i = 0; i < WAITER_COUNT; ++i) {
      waiter_args[i].event = broadcast_event;
      waiter_args[i].result = WAIT_FAILED;
      waiter_handles[i] = CreateThread(NULL, 0, wait_event_long_worker, &waiter_args[i], 0, NULL);
      assert(waiter_handles[i] != NULL);
   }
   assert(SetEvent(broadcast_event) == TRUE);
   for (int i = 0; i < WAITER_COUNT; ++i) {
      assert(WaitForSingleObject(waiter_handles[i], 2000) == WAIT_OBJECT_0);
      assert(waiter_args[i].result == WAIT_OBJECT_0);
      assert(CloseHandle(waiter_handles[i]) == TRUE);
   }
   assert(CloseHandle(broadcast_event) == TRUE);

   /* Concurrent registry/lifetime stress: several threads repeatedly create,
    * signal, wait on, and close events while contending on one Win32 mutex. */
   enum { STRESS_THREADS = 8, STRESS_ITERATIONS = 150 };
   HANDLE stress_mutex = CreateMutex(NULL, FALSE, NULL);
   assert(stress_mutex != NULL);
   HANDLE stress_threads[STRESS_THREADS];
   stress_args stress[STRESS_THREADS];
   unsigned long stress_counter = 0;
   for (int i = 0; i < STRESS_THREADS; ++i) {
      stress[i].mutex = stress_mutex;
      stress[i].iterations = STRESS_ITERATIONS;
      stress[i].counter = &stress_counter;
      stress_threads[i] = CreateThread(NULL, 0, concurrent_handle_stress, &stress[i], 0, NULL);
      assert(stress_threads[i] != NULL);
   }
   for (int i = 0; i < STRESS_THREADS; ++i) {
      assert(WaitForSingleObject(stress_threads[i], 10000) == WAIT_OBJECT_0);
      assert(CloseHandle(stress_threads[i]) == TRUE);
   }
   assert(stress_counter == (unsigned long)STRESS_THREADS * STRESS_ITERATIONS);
   assert(CloseHandle(stress_mutex) == TRUE);
#endif

   /* dimensioni dei tipi come in Windows */
   assert(sizeof(DWORD) == 4 && sizeof(LONG) == 4 && sizeof(BOOL) == 4);
   assert(sizeof(FILETIME) == 8 && sizeof(LARGE_INTEGER) == 8);
   assert(sizeof(BYTE) == 1 && sizeof(WORD) == 2);

   SetLastError(5);
   pthread_t t; pthread_create(&t, NULL, worker, NULL); pthread_join(t, NULL);
   assert(GetLastError() == 5);

   SYSTEMTIME st; GetSystemTime(&st);
   printf("GetSystemTime: %04u-%02u-%02u %02u:%02u:%02u.%03u\n",
          st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);

   {
      FILETIME idle_time = {0}, kernel_time = {0}, user_time = {0};
      assert(GetSystemTimes(&idle_time, &kernel_time, &user_time) == TRUE);
      assert(GetLastError() == ERROR_SUCCESS);
      assert(smoke_filetime_ticks(&idle_time) + smoke_filetime_ticks(&kernel_time) +
             smoke_filetime_ticks(&user_time) > 0);
      assert(smoke_filetime_ticks(&kernel_time) >= smoke_filetime_ticks(&idle_time));
      assert(GetSystemTimes(NULL, NULL, NULL) == TRUE);
      assert(GetLastError() == ERROR_SUCCESS);
   }

   LARGE_INTEGER f, c1, c2;
   QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c1); Sleep(20); QueryPerformanceCounter(&c2);
   printf("QPC freq=%lld, delta=%lld ticks (~%.1f ms)\n", (long long)f.QuadPart,
          (long long)(c2.QuadPart - c1.QuadPart), 1000.0 * (c2.QuadPart - c1.QuadPart) / f.QuadPart);

   MEMORYSTATUSEX ms; ms.dwLength = sizeof(ms);
   assert(GlobalMemoryStatusEx(&ms) == TRUE);
   assert(ms.dwMemoryLoad <= 100u);
   assert(ms.ullAvailPhys <= ms.ullTotalPhys);
   assert(ms.ullAvailPageFile <= ms.ullTotalPageFile);
   assert(ms.ullAvailVirtual <= ms.ullTotalVirtual);
   printf("RAM tot=%llu MB, libera=%llu MB, dwMemoryLoad=%u%%\n",
          (unsigned long long)ms.ullTotalPhys >> 20, (unsigned long long)ms.ullAvailPhys >> 20, ms.dwMemoryLoad);

   /* Ricerca file: il primo risultato non deve essere ripetuto da _findnext. */
   char template[] = "/tmp/winapi-smoke-XXXXXX";
   char *dir = mkdtemp(template);
   assert(dir != NULL);
   char path1[512], path2[512], pattern[512], all_pattern[512], nested_dir[512], hidden_dir[512];
   snprintf(path1, sizeof(path1), "%s/alpha.txt", dir);
   snprintf(path2, sizeof(path2), "%s/beta.txt", dir);
   snprintf(pattern, sizeof(pattern), "%s/*.txt", dir);
   snprintf(all_pattern, sizeof(all_pattern), "%s/*", dir);
   snprintf(nested_dir, sizeof(nested_dir), "%s/nested", dir);
   snprintf(hidden_dir, sizeof(hidden_dir), "%s/.hidden-dir", dir);
   assert(mkdir(nested_dir, 0700) == 0);
   assert(mkdir(hidden_dir, 0700) == 0);
   assert((GetFileAttributes(hidden_dir) & FILE_ATTRIBUTE_HIDDEN) != 0);
   /* CRT adapters must preserve standard C error semantics and remove() must
    * support empty directories, not only regular files. */
   errno = 0;
   assert(win_mkdir(nested_dir) == -1 && errno == EEXIST);
   errno = 0;
   assert(win_rmdir("/definitely-not-a-babyc-directory") == -1 && errno == ENOENT);
   errno = 0;
   assert(win_mkdir("") == -1 && errno == ENOENT);
   errno = 0;
   assert(win_remove("") == -1 && errno == ENOENT);
   errno = 0;
   assert(win_fopen(NULL, "w") == NULL && errno == EINVAL);
   char wrapper_dir[512];
   snprintf(wrapper_dir, sizeof(wrapper_dir), "%s/remove-dir", dir);
   assert(mkdir(wrapper_dir, 0700) == 0);
   assert(win_remove(wrapper_dir) == 0);
   int fd = open(path1, O_CREAT | O_WRONLY, 0600);
   assert(fd >= 0); close(fd);
   fd = open(path2, O_CREAT | O_WRONLY, 0600);
   assert(fd >= 0); close(fd);
   {
      char link_path[512];
      struct stat before_link, after_link;
      snprintf(link_path, sizeof(link_path), "%s/alpha-link", dir);
      assert(symlink(path1, link_path) == 0);
      assert(stat(path1, &before_link) == 0);
      assert(SetFileAttributes(link_path, FILE_ATTRIBUTE_READONLY) == FALSE);
      assert(GetLastError() == ERROR_NOT_SUPPORTED);
      assert(stat(path1, &after_link) == 0);
      assert((before_link.st_mode & 07777) == (after_link.st_mode & 07777));
      assert(unlink(link_path) == 0);
   }
   /* FindFirstFile and FindNextFile must both preserve stat nanoseconds down
    * to 100ns FILETIME ticks, independently of directory enumeration order. */
   {
      struct timespec alpha_times[2] = {
         { (time_t)946684800, 123456789L }, { (time_t)951782400, 987654321L }
      };
      struct timespec beta_times[2] = {
         { (time_t)978307200, 765432198L }, { (time_t)1009843200, 456789123L }
      };
      assert(utimensat(AT_FDCWD, path1, alpha_times, 0) == 0);
      assert(utimensat(AT_FDCWD, path2, beta_times, 0) == 0);
   }
   struct _finddata_t found;
   errno = 0;
   assert(_findfirst(NULL, &found) == -1);
   assert(errno == EINVAL);
   errno = 0;
   assert(_findfirst(pattern, NULL) == -1);
   assert(errno == EINVAL);
   intptr_t search = _findfirst(pattern, &found);
   assert(search != -1);
   assert(found.name[0] != '\0');
   char first_name[MAX_PATH];
   snprintf(first_name, sizeof(first_name), "%s", found.name);
   assert(_findnext(search, &found) == 0);
   assert(strcmp(first_name, found.name) != 0);
   assert(_findnext(search, &found) == -1);
   assert(_findclose(search) == 0);

   /* Win32 search handles must be opaque tokens in the shared HANDLE namespace,
    * not heap pointers that can collide with or be confused for other handles. */
   WIN32_FIND_DATA find_data;
   char missing_pattern[512];
   snprintf(missing_pattern, sizeof(missing_pattern), "%s/no-such-*.txt", dir);
   SetLastError(ERROR_SUCCESS);
   assert(FindFirstFile(missing_pattern, &find_data) == INVALID_HANDLE_VALUE);
   assert(GetLastError() == ERROR_FILE_NOT_FOUND);
   HANDLE search_handle = FindFirstFile(pattern, &find_data);
   assert(search_handle != INVALID_HANDLE_VALUE && search_handle != NULL);
   {
      unsigned int seen = 0;
      do {
         uint64_t expected_access, expected_write;
         if (strcmp(find_data.cFileName, "alpha.txt") == 0) {
            expected_access = smoke_expected_filetime((time_t)946684800, 123456789L);
            expected_write = smoke_expected_filetime((time_t)951782400, 987654321L);
         } else {
            assert(strcmp(find_data.cFileName, "beta.txt") == 0);
            expected_access = smoke_expected_filetime((time_t)978307200, 765432198L);
            expected_write = smoke_expected_filetime((time_t)1009843200, 456789123L);
         }
         assert(smoke_filetime_ticks(&find_data.ftLastAccessTime) == expected_access);
         assert(smoke_filetime_ticks(&find_data.ftLastWriteTime) == expected_write);
         ++seen;
      } while (FindNextFile(search_handle, &find_data) == TRUE);
      assert(GetLastError() == ERROR_NO_MORE_FILES);
      assert(seen == 2u);
   }
   HANDLE file_handle = CreateFile(path1, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
   assert(file_handle != INVALID_HANDLE_VALUE && file_handle != NULL);
   assert(search_handle != file_handle);
   assert(FindClose(search_handle) == TRUE);

   /* GLOB_MARK appends '/' to directories. The Win32 basename must not become
    * empty after stripping that marker, and directory attributes must survive. */
   search_handle = FindFirstFile(all_pattern, &find_data);
   assert(search_handle != INVALID_HANDLE_VALUE && search_handle != NULL);
   {
      int found_nested = 0, found_hidden = 0;
      do {
         assert(find_data.cFileName[0] != '\0');
         if (strcmp(find_data.cFileName, "nested") == 0) {
            found_nested = 1;
            assert((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
         }
         if (strcmp(find_data.cFileName, ".hidden-dir") == 0) {
            found_hidden = 1;
            assert((find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
            assert((find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0);
         }
      } while (FindNextFile(search_handle, &find_data) == TRUE);
      assert(GetLastError() == ERROR_NO_MORE_FILES);
      assert(found_nested && found_hidden);
   }
   assert(FindClose(search_handle) == TRUE);

   search_handle = FindFirstFile(pattern, &find_data);
   assert(search_handle != INVALID_HANDLE_VALUE && search_handle != NULL);
   SetLastError(ERROR_SUCCESS);
   assert(CloseHandle(search_handle) == FALSE);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(FindNextFile(search_handle, &find_data) == TRUE);
   assert(FindNextFile(search_handle, &find_data) == FALSE);
   assert(GetLastError() == ERROR_NO_MORE_FILES);
   assert(FindClose(search_handle) == TRUE);
   assert(FindNextFile(search_handle, &find_data) == FALSE);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(CloseHandle(file_handle) == TRUE);

   /* Heavy regression: race multiple FindNextFile calls against FindClose.
    * The API calls are intentionally concurrent; their results depend on
    * mutex acquisition order, but the close must succeed exactly once and
    * every operation after the close must reject the stale token. */
   for (unsigned int iteration = 0; iteration < 100; ++iteration) {
      HANDLE raced = FindFirstFile(pattern, &find_data);
      assert(raced != INVALID_HANDLE_VALUE && raced != NULL);
      find_race_gate gate = {
         .mutex = PTHREAD_MUTEX_INITIALIZER,
         .cond = PTHREAD_COND_INITIALIZER,
         .ready = 0,
         .total = 4,
         .released = 0
      };
      pthread_t threads[4];
      find_race_args args[4];
      for (unsigned int i = 0; i < 4; ++i) {
         args[i].gate = &gate;
         args[i].handle = raced;
         args[i].close_operation = (i == 3);
         args[i].result = FALSE;
         args[i].last_error = ERROR_SUCCESS;
         assert(pthread_create(&threads[i], NULL, find_next_close_race_worker, &args[i]) == 0);
      }
      assert(pthread_mutex_lock(&gate.mutex) == 0);
      while (gate.ready != gate.total)
         assert(pthread_cond_wait(&gate.cond, &gate.mutex) == 0);
      gate.released = 1;
      assert(pthread_cond_broadcast(&gate.cond) == 0);
      assert(pthread_mutex_unlock(&gate.mutex) == 0);
      for (unsigned int i = 0; i < 4; ++i)
         assert(pthread_join(threads[i], NULL) == 0);
      assert(args[3].result == TRUE);
      unsigned int successful_next_calls = 0;
      for (unsigned int i = 0; i < 3; ++i) {
         if (args[i].result == TRUE) {
            ++successful_next_calls;
         } else {
            assert(args[i].last_error == ERROR_NO_MORE_FILES ||
                   args[i].last_error == ERROR_INVALID_HANDLE);
         }
      }
      /* FindFirst already consumed one of the two fixture files, so at most
       * one concurrent FindNext can successfully consume the remaining one. */
      assert(successful_next_calls <= 1);
      assert(FindNextFile(raced, &find_data) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(FindClose(raced) == FALSE);
      assert(GetLastError() == ERROR_INVALID_HANDLE);
      assert(pthread_cond_destroy(&gate.cond) == 0);
      assert(pthread_mutex_destroy(&gate.mutex) == 0);
   }

   unlink(path1); unlink(path2); rmdir(nested_dir); rmdir(hidden_dir); rmdir(dir);

   /* A failed buffered write must remain observable through the CRT fclose
    * adapter instead of being swallowed by its extra fflush/fdatasync calls. */
   {
      FILE *full = fopen("/dev/full", "w");
      if (full != NULL) {
         assert(fwrite("must fail", 1, 9, full) == 9);
         errno = 0;
         assert(win_fclose(full) == EOF);
         assert(errno != 0);
      }
      FILE *null_stream = win_fopen("/dev/null", "w");
      if (null_stream != NULL) assert(win_fclose(null_stream) == 0);
      /* Read-only procfs streams must not be treated as failed durability
       * operations just because fdatasync is unsupported on pseudo-files. */
      FILE *proc_stream = win_fopen("/proc/stat", "r");
      if (proc_stream != NULL) assert(win_fclose(proc_stream) == 0);
   }

   /* GetLogicalDriveStrings must return root paths with trailing backslashes
    * and the Win32 double-NUL terminator, including correct required sizing. */
   {
      DWORD required = GetLogicalDriveStrings(0, NULL);
      assert(required >= 2);
      char *drives = (char *)malloc(required);
      assert(drives != NULL);
      assert(GetLogicalDriveStrings(required, drives) == required - 1);
      assert(drives[required - 1] == '\0');
      assert(drives[required - 2] == '\0');
      size_t offset = 0;
      while (offset + 1 < required && drives[offset] != '\0') {
         size_t length = strlen(drives + offset);
         assert(length >= 3);
         assert(drives[offset + length - 1] == '\\');
         offset += length + 1;
      }
      assert(offset < required && drives[offset] == '\0');
      free(drives);
   }

   /* GetCurrentDirectory rispetta il contratto di dimensione del buffer. */
   char tiny[1] = {'X'};
   DWORD cwd_required = GetCurrentDirectory(1, tiny);
   assert(cwd_required > 1);
   assert(tiny[0] == 'X');
   assert(GetLastError() == ERROR_INSUFFICIENT_BUFFER);
   char cwd[4096];
   DWORD cwd_length = GetCurrentDirectory(sizeof(cwd), cwd);
   assert(cwd_length > 0 && cwd_length < sizeof(cwd));
   assert(cwd[cwd_length] == '\0');
   assert(GetLastError() == ERROR_SUCCESS);
   assert(SetCurrentDirectory(NULL) == FALSE);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(SetCurrentDirectory("/definitely-not-a-babyc-directory") == FALSE);
   assert(GetLastError() == ERROR_PATH_NOT_FOUND);

   /* GetDiskFreeSpaceEx consente gli output opzionali NULL e aggiorna
    * GetLastError in modo deterministico sia sul successo sia sul fallimento. */
   assert(GetDiskFreeSpaceEx(".", NULL, NULL, NULL) == TRUE);
   assert(GetLastError() == ERROR_SUCCESS);
   {
      ULARGE_INTEGER available_bytes, total_bytes, free_bytes;
      assert(GetDiskFreeSpaceEx("/tmp", &available_bytes, &total_bytes, &free_bytes) == TRUE);
      assert(GetLastError() == ERROR_SUCCESS);
      assert(available_bytes.QuadPart <= free_bytes.QuadPart);
      assert(free_bytes.QuadPart <= total_bytes.QuadPart);
      assert(total_bytes.QuadPart > 0);
   }
   ULARGE_INTEGER free_before, total_before, total_free_before;
   free_before.QuadPart = 11;
   total_before.QuadPart = 22;
   total_free_before.QuadPart = 33;
   assert(GetDiskFreeSpaceEx("/definitely-not-a-babyc-directory",
       &free_before, &total_before, &total_free_before) == FALSE);
   assert(GetLastError() == ERROR_PATH_NOT_FOUND);
   assert(free_before.QuadPart == 11 && total_before.QuadPart == 22 &&
          total_free_before.QuadPart == 33);

   /* Conversioni dei percorsi: separatori misti e argomenti nulli. */
   char *converted = win2posix_path("folder\\sub//file.txt");
   assert(converted != NULL);
   assert(strcmp(converted, "folder/sub/file.txt") == 0);
   free(converted);
   converted = posix2win_path("folder/sub/file.txt");
   assert(converted != NULL);
   assert(strcmp(converted, "folder\\sub\\file.txt") == 0);
   free(converted);
   errno = 0;
   assert(win2posix_path(NULL) == NULL && errno == EINVAL);
   errno = 0;
   assert(posix2win_path(NULL) == NULL && errno == EINVAL);
   char drv[4], dirpart[128], fname[128], ext[32];
   _splitpath("C:\\work\\sample.test.c", drv, dirpart, fname, ext);
   assert(strcmp(drv, "C:") == 0);
   assert(strcmp(dirpart, "\\work\\") == 0);
   assert(strcmp(fname, "sample.test") == 0);
   assert(strcmp(ext, ".c") == 0);
   _splitpath("folder/sub/sample.test.c", drv, dirpart, fname, ext);
   assert(strcmp(dirpart, "folder\\sub\\") == 0);
   assert(strcmp(fname, "sample.test") == 0 && strcmp(ext, ".c") == 0);
   _splitpath(NULL, drv, dirpart, fname, ext);
   assert(drv[0] == '\0' && dirpart[0] == '\0' && fname[0] == '\0' && ext[0] == '\0');

#if defined(ENABLE_WSHMEM) && ENABLE_WSHMEM
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   /* Failure to acquire the per-name lock must fail closed before creating
    * or publishing any POSIX shared-memory object. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_flock_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      winapi_test_shmem_fail_next_flock_ex();
      assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      int probe = shm_open(posix_name, O_RDONLY, 0);
      assert(probe < 0 && errno == ENOENT);
   }

   /* If explicit LOCK_UN fails on return, closing the descriptor must still
    * release the kernel lock. A subsequent operation on the same name proves
    * the lock domain remains usable. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_flock_un_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      winapi_test_shmem_fail_next_flock_un();
      HANDLE created = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(created != NULL);
      assert(CloseHandle(created) == TRUE);
      HANDLE opened = OpenFileMapping(FILE_MAP_READ, FALSE, name);
      assert(opened != NULL);
      assert(CloseHandle(opened) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* If unlink fails while cleaning up an object whose fchmod failed, the
    * INITIALIZING record must make the next caller retry cleanup before
    * creating a fresh mapping. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_unlink_cleanup_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      winapi_test_shmem_fail_next_fchmod();
      winapi_test_shmem_fail_next_shm_unlink();
      assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      int probe = shm_open(posix_name, O_RDONLY, 0);
      assert(probe >= 0);
      close(probe);
      HANDLE retry = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(retry != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(retry) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* An injected create-time shm_open failure leaves no published object;
    * the INITIALIZING record is recoverable on the next call. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_shmopen_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      winapi_test_shmem_fail_next_shm_open_create();
      assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      int probe = shm_open(posix_name, O_RDONLY, 0);
      assert(probe < 0 && errno == ENOENT);
      HANDLE retry = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(retry != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(retry) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* A create-time fchmod failure must close and unlink the partially
    * initialized object. The next call recovers INITIALIZING and succeeds. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_fchmod_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      winapi_test_shmem_fail_next_fchmod();
      assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      int probe = shm_open(posix_name, O_RDONLY, 0);
      assert(probe < 0 && errno == ENOENT);
      HANDLE retry = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(retry != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(retry) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* A read error on an existing coordination record is rejected without
    * opening the mapping; after the one-shot fault, normal open still works. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_pread_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      HANDLE created = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(created != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(created) == TRUE);
      winapi_test_shmem_fail_next_pread();
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      HANDLE opened = OpenFileMapping(FILE_MAP_READ, FALSE, name);
      assert(opened != NULL);
      assert(CloseHandle(opened) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* Force a partial first record write followed by EIO. The implementation
    * must roll the lock record back to empty; a later creator must succeed. */
   {
      char fault_name[96], fault_posix_name[100];
      snprintf(fault_name, sizeof(fault_name), "winapi_pwrite_fault_%ld", (long)getpid());
      winapi_test_shmem_fail_pwrite_after(8);
      HANDLE failed = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, fault_name);
      assert(failed == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      snprintf(fault_posix_name, sizeof(fault_posix_name), "/%s", fault_name);
      int probe = shm_open(fault_posix_name, O_RDONLY, 0);
      assert(probe < 0 && errno == ENOENT);
      HANDLE retry = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, fault_name);
      assert(retry != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(retry) == TRUE);
      assert(shm_unlink(fault_posix_name) == 0);
   }
#endif
   /* If rollback itself fails after a partial record write, the next opener
    * must fail closed rather than treating the damaged record as initialized.
    * Remove the test-owned lock file only after both API calls have returned. */
   {
      char fault_name[96], lock_dir[96], lock_path[256], posix_name[100];
      uint64_t hash = UINT64_C(1469598103934665603);
      uint64_t hash_b = UINT64_C(0x9e3779b97f4a7c15);
      const unsigned char *np;
      int lock_fd, probe;
      snprintf(fault_name, sizeof(fault_name), "winapi_rollback_fault_%ld", (long)getpid());
      np = (const unsigned char *)fault_name;
      while (*np != 0) {
         hash ^= *np;
         hash *= UINT64_C(1099511628211);
         hash_b ^= (uint64_t)*np + UINT64_C(0x9e3779b97f4a7c15) + (hash_b << 6) + (hash_b >> 2);
         ++np;
      }
      snprintf(lock_dir, sizeof(lock_dir), "/tmp/winapi-shm-locks-%lu", (unsigned long)getuid());
      snprintf(lock_path, sizeof(lock_path), "%s/lock-%016llx-%016llx", lock_dir,
         (unsigned long long)hash, (unsigned long long)hash_b);
      snprintf(posix_name, sizeof(posix_name), "/%s", fault_name);
      winapi_test_shmem_fail_pwrite_after(8);
      winapi_test_shmem_fail_rollback_ftruncate();
      HANDLE failed = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, fault_name);
      assert(failed == NULL && GetLastError() == ERROR_GEN_FAILURE);
      probe = shm_open(posix_name, O_RDONLY, 0);
      assert(probe < 0 && errno == ENOENT);
      /* The damaged record is intentionally rejected on subsequent access. */
      assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, fault_name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      lock_fd = open(lock_path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
      assert(lock_fd >= 0);
      struct stat lock_st;
      assert(fstat(lock_fd, &lock_st) == 0 && lock_st.st_size == 8);
      assert(close(lock_fd) == 0);
      assert(unlink(lock_path) == 0);
   }

   /* Fail the fstat used to inspect an existing named POSIX mapping, after
    * the three metadata checks used to validate/acquire its lock. The failed
    * open must close its descriptor and a retry must still succeed. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_fstat_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      HANDLE created = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(created != NULL && CloseHandle(created) == TRUE);
      winapi_test_shmem_fail_fstat_after(3);
      assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      HANDLE reopened = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(reopened != NULL);
      assert(CloseHandle(reopened) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* Allocation failure after opening a named mapping must close the newly
    * acquired fd and leave no registry entry. The next open must work. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_alloc_fault_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      HANDLE created = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(created != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(created) == TRUE);
      winapi_test_shmem_fail_next_calloc();
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, name) == NULL);
      assert(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
      HANDLE opened = OpenFileMapping(FILE_MAP_READ, FALSE, name);
      assert(opened != NULL);
      assert(CloseHandle(opened) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* Fail the second allocation in OpenFileMapping: the mapping wrapper is
    * allocated, registry-entry allocation fails, and cleanup must retire the
    * wrapper and close the fd. A later open proves the registry remains sane. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_alloc_second_open_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      HANDLE created = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(created != NULL && CloseHandle(created) == TRUE);
      winapi_test_shmem_fail_calloc_after(1);
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, name) == NULL);
      assert(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
      HANDLE opened = OpenFileMapping(FILE_MAP_READ, FALSE, name);
      assert(opened != NULL && CloseHandle(opened) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* Fail the second allocation after mmap succeeds. The view must be unmapped
    * and no view-registry entry may remain; a subsequent view must work. */
   {
      char name[96], posix_name[100];
      snprintf(name, sizeof(name), "winapi_alloc_second_view_%ld", (long)getpid());
      snprintf(posix_name, sizeof(posix_name), "/%s", name);
      HANDLE mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
         0, 4096, name);
      assert(mapping != NULL);
      winapi_test_shmem_fail_calloc_after(1);
      assert(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096) == NULL);
      assert(GetLastError() == ERROR_NOT_ENOUGH_MEMORY);
      unsigned char *view = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      assert(view != NULL);
      assert(UnmapViewOfFile(view) == TRUE);
      assert(CloseHandle(mapping) == TRUE);
      assert(shm_unlink(posix_name) == 0);
   }

   /* A truncated coordination record must fail closed rather than be treated
    * as an empty/READY record. The test removes the private lock file after
    * observing the expected error so later tests can use the same name. */
   {
      char corrupt_name[96], lock_dir[96], lock_path[256];
      uint64_t hash = UINT64_C(1469598103934665603);
      uint64_t hash_b = UINT64_C(0x9e3779b97f4a7c15);
      const unsigned char *np;
      int corrupt_fd;
      snprintf(corrupt_name, sizeof(corrupt_name), "winapi_corrupt_lock_%ld", (long)getpid());
      np = (const unsigned char *)corrupt_name;
      while (*np != 0) {
         hash ^= *np;
         hash *= UINT64_C(1099511628211);
         hash_b ^= (uint64_t)*np + UINT64_C(0x9e3779b97f4a7c15) + (hash_b << 6) + (hash_b >> 2);
         ++np;
      }
      snprintf(lock_dir, sizeof(lock_dir), "/tmp/winapi-shm-locks-%lu", (unsigned long)getuid());
      assert(mkdir(lock_dir, S_IRWXU) == 0 || errno == EEXIST);
      snprintf(lock_path, sizeof(lock_path), "%s/lock-%016llx-%016llx", lock_dir,
         (unsigned long long)hash, (unsigned long long)hash_b);
      corrupt_fd = open(lock_path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
         S_IRUSR | S_IWUSR);
      assert(corrupt_fd >= 0);
      assert(write(corrupt_fd, "x", 1) == 1);
      assert(close(corrupt_fd) == 0);
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, corrupt_name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      assert(unlink(lock_path) == 0);

      /* A lock file with group/other permissions is rejected even if owned by
       * this uid: the coordination namespace must remain private. */
      corrupt_fd = open(lock_path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
         S_IRUSR | S_IWUSR);
      assert(corrupt_fd >= 0);
      assert(fchmod(corrupt_fd, S_IRUSR | S_IWUSR | S_IRGRP) == 0);
      assert(close(corrupt_fd) == 0);
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, corrupt_name) == NULL);
      assert(GetLastError() == ERROR_GEN_FAILURE);
      assert(unlink(lock_path) == 0);

      /* A correctly sized record for a different name must not be accepted
       * merely because its two-hash path collides or was substituted. */
      {
         struct test_shm_lock_record {
            uint64_t magic;
            char name[MAX_PATH + 1];
            unsigned char state;
         } record;
         corrupt_fd = open(lock_path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
         assert(corrupt_fd >= 0);
         memset(&record, 0, sizeof(record));
         record.magic = UINT64_C(0x57494e53484d3031);
         snprintf(record.name, sizeof(record.name), "%s", "winapi_foreign_record_name");
         record.state = 0u;
         assert(write(corrupt_fd, &record, sizeof(record)) == (ssize_t)sizeof(record));
         assert(close(corrupt_fd) == 0);
         assert(OpenFileMapping(FILE_MAP_READ, FALSE, corrupt_name) == NULL);
         assert(GetLastError() == ERROR_GEN_FAILURE);
         assert(unlink(lock_path) == 0);
      }

      /* Unknown state values are malformed records, not READY by default. */
      {
         struct test_shm_lock_record {
            uint64_t magic;
            char name[MAX_PATH + 1];
            unsigned char state;
         } record;
         corrupt_fd = open(lock_path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
            S_IRUSR | S_IWUSR);
         assert(corrupt_fd >= 0);
         memset(&record, 0, sizeof(record));
         record.magic = UINT64_C(0x57494e53484d3031);
         snprintf(record.name, sizeof(record.name), "%s", corrupt_name);
         record.state = 0xffu;
         assert(write(corrupt_fd, &record, sizeof(record)) == (ssize_t)sizeof(record));
         assert(close(corrupt_fd) == 0);
         assert(OpenFileMapping(FILE_MAP_READ, FALSE, corrupt_name) == NULL);
         assert(GetLastError() == ERROR_GEN_FAILURE);
         assert(unlink(lock_path) == 0);
      }
   }

   /* File-backed mappings accept only registered CreateFile handles. NULL and
    * arbitrary handles must fail without being interpreted as POSIX descriptors. */
   SetLastError(ERROR_SUCCESS);
   assert(CreateFileMapping(NULL, NULL, PAGE_READWRITE, 0, 4096, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   SetLastError(ERROR_SUCCESS);
   assert(CreateFileMapping((HANDLE)(uintptr_t)0x1234, NULL, PAGE_READWRITE, 0, 4096, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_HANDLE);

   /* A raw POSIX descriptor is not a WinAPI HANDLE. Rejection must not close
    * or otherwise consume a descriptor owned by the caller. */
   int raw_fd = open("/dev/null", O_RDONLY);
   assert(raw_fd >= 0);
   assert(CreateFileMapping((HANDLE)(intptr_t)raw_fd, NULL, PAGE_READWRITE, 0, 4096, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(fcntl(raw_fd, F_GETFD) != -1);
   assert(close(raw_fd) == 0);

   /* File-backed mappings duplicate the registered file descriptor: closing
    * the original file handle must not invalidate an existing mapping. */
   char backing_path[] = "/tmp/winapi_file_mapping_XXXXXX";
   int backing_raw = mkstemp(backing_path);
   assert(backing_raw >= 0);
   assert(close(backing_raw) == 0);
   HANDLE backing_file = CreateFile(backing_path, GENERIC_READ | GENERIC_WRITE,
      0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(backing_file != INVALID_HANDLE_VALUE);
   const char initial_bytes[] = "mapping-data";
   DWORD io_count = 0;
   assert(WriteFile(backing_file, initial_bytes, (DWORD)(sizeof(initial_bytes) - 1), &io_count, NULL));
   assert(io_count == sizeof(initial_bytes) - 1);
   HANDLE file_mapping = CreateFileMapping(backing_file, NULL, PAGE_READWRITE, 0, 0, NULL);
   assert(file_mapping != NULL);
   unsigned char *file_view = (unsigned char *)MapViewOfFile(file_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 0);
   assert(file_view != NULL);
   assert(memcmp(file_view, initial_bytes, sizeof(initial_bytes) - 1) == 0);
   file_view[0] = 'M';
   assert(CloseHandle(backing_file) == TRUE);
   assert(file_view[0] == 'M');
   assert(UnmapViewOfFile(file_view) == TRUE);
   assert(CloseHandle(file_mapping) == TRUE);

   /* A zero-length file cannot be mapped using its current size. A
    * PAGE_READONLY mapping must not extend it even if the source handle itself
    * is writable. */
   char empty_backing_path[] = "/tmp/winapi_empty_mapping_XXXXXX";
   int empty_backing_raw = mkstemp(empty_backing_path);
   assert(empty_backing_raw >= 0);
   assert(close(empty_backing_raw) == 0);
   HANDLE empty_backing = CreateFile(empty_backing_path, GENERIC_READ | GENERIC_WRITE,
      0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(empty_backing != INVALID_HANDLE_VALUE);
   assert(CreateFileMapping(empty_backing, NULL, PAGE_READONLY, 0, 4096, NULL) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   LARGE_INTEGER empty_size;
   assert(GetFileSizeEx(empty_backing, &empty_size) == TRUE);
   assert(empty_size.QuadPart == 0);
   assert(CreateFileMapping(empty_backing, NULL, PAGE_READWRITE, 0, 0, NULL) == NULL);
   assert(GetLastError() == ERROR_FILE_INVALID);
   assert(CloseHandle(empty_backing) == TRUE);
   unlink(empty_backing_path);

   /* A requested size larger than the file extends it when the file handle
    * is writable; the extension is visible through the original file handle. */
   backing_file = CreateFile(backing_path, GENERIC_READ | GENERIC_WRITE,
      0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(backing_file != INVALID_HANDLE_VALUE);
   file_mapping = CreateFileMapping(backing_file, NULL, PAGE_READWRITE, 0, 8192, NULL);
   assert(file_mapping != NULL);
   LARGE_INTEGER backing_size;
   assert(GetFileSizeEx(backing_file, &backing_size) == TRUE);
   assert(backing_size.QuadPart == 8192);
   file_view = (unsigned char *)MapViewOfFile(file_mapping, FILE_MAP_ALL_ACCESS, 0, 4096, 4096);
   assert(file_view != NULL);
   file_view[0] = 0x6d;
   assert(UnmapViewOfFile(file_view) == TRUE);
   assert(CloseHandle(file_mapping) == TRUE);
   assert(CloseHandle(backing_file) == TRUE);

   /* Detect a backing file truncated after the mapping handle was created.
    * Existing views cannot be made safe against external truncation, but new
    * views must fail rather than expose a mapping that can SIGBUS past EOF. */
   backing_file = CreateFile(backing_path, GENERIC_READ | GENERIC_WRITE,
      0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(backing_file != INVALID_HANDLE_VALUE);
   file_mapping = CreateFileMapping(backing_file, NULL, PAGE_READWRITE, 0, 8192, NULL);
   assert(file_mapping != NULL);
   /* Truncate through an independent POSIX descriptor to model external code. */
   int truncate_fd = open(backing_path, O_WRONLY);
   assert(truncate_fd >= 0);
   assert(ftruncate(truncate_fd, 4096) == 0);
   assert(close(truncate_fd) == 0);
   assert(MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 4096, 4096) == NULL);
   assert(GetLastError() == ERROR_FILE_INVALID);
   assert(CloseHandle(file_mapping) == TRUE);
   assert(CloseHandle(backing_file) == TRUE);

   backing_file = CreateFile(backing_path, GENERIC_READ, 0, NULL,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(backing_file != INVALID_HANDLE_VALUE);
   assert(CreateFileMapping(backing_file, NULL, PAGE_READWRITE, 0, 0, NULL) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   file_mapping = CreateFileMapping(backing_file, NULL, PAGE_READONLY, 0, 0, NULL);
   assert(file_mapping != NULL);
   file_view = (unsigned char *)MapViewOfFile(file_mapping, FILE_MAP_READ, 0, 0, 0);
   assert(file_view != NULL && file_view[0] == 'M');
   assert(MapViewOfFile(file_mapping, FILE_MAP_WRITE, 0, 0, 1) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   assert(UnmapViewOfFile(file_view) == TRUE);
   assert(CloseHandle(file_mapping) == TRUE);
   assert(CloseHandle(backing_file) == TRUE);
   unlink(backing_path);

   /* File-backed mappings with names are deliberately not supported until a
    * cross-process file-mapping metadata protocol is implemented. */
   char named_backing_path[] = "/tmp/winapi_named_file_mapping_XXXXXX";
   int named_backing_raw = mkstemp(named_backing_path);
   assert(named_backing_raw >= 0);
   assert(close(named_backing_raw) == 0);
   backing_file = CreateFile(named_backing_path, GENERIC_READ | GENERIC_WRITE,
      0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
   assert(backing_file != INVALID_HANDLE_VALUE);
   assert(CreateFileMapping(backing_file, NULL, PAGE_READWRITE, 0, 4096, "unsupported_file_mapping_name") == NULL);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);
   assert(CloseHandle(backing_file) == TRUE);
   unlink(named_backing_path);

   /* Invalid mapping sizes and POSIX-incompatible names fail predictably. */
   SetLastError(ERROR_SUCCESS);
   assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 0, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, "bad/name") == NULL);
   assert(GetLastError() == ERROR_INVALID_NAME);
   assert(CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_EXECUTE_READ, 0, 4096, NULL) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   int unsupported_mapping_sa;
   memset(&unsupported_mapping_sa, 0, sizeof(unsupported_mapping_sa));
   assert(CreateFileMapping(INVALID_HANDLE_VALUE, &unsupported_mapping_sa, PAGE_READWRITE, 0, 4096, NULL) == NULL);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

   /* Only explicitly supported mapping rights are accepted. */
   assert(OpenFileMapping(FILE_MAP_COPY, FALSE, "winapi-no-such-mapping") == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(OpenFileMapping(FILE_MAP_READ, TRUE, "winapi-no-such-mapping") == NULL);
   assert(GetLastError() == ERROR_CALL_NOT_IMPLEMENTED);

   /* Recovery from a creator that died with a published, zero-length object.
      The next opener must discard the stale object; a subsequent creator must
      be able to create a clean mapping under the same name. */
   {
      char stale_name[96];
      snprintf(stale_name, sizeof(stale_name), "winapi_stale_init_%ld", (long)getpid());
      pid_t stale_child = fork();
      assert(stale_child >= 0);
      if (stale_child == 0) {
         execl(argv[0], argv[0], "--shmem-stale-init-child", stale_name, (char *)NULL);
         _exit(54);
      }
      int stale_status = 0;
      assert(waitpid(stale_child, &stale_status, 0) == stale_child);
      assert(WIFEXITED(stale_status) && WEXITSTATUS(stale_status) == 0);
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, stale_name) == NULL);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      HANDLE recovered = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, stale_name);
      assert(recovered != NULL && GetLastError() == ERROR_SUCCESS);
      unsigned char *recovered_view = (unsigned char *)MapViewOfFile(recovered, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      assert(recovered_view != NULL);
      recovered_view[0] = 0x6b;
      assert(UnmapViewOfFile(recovered_view) == TRUE);
      assert(CloseHandle(recovered) == TRUE);
      char stale_posix_name[100];
      snprintf(stale_posix_name, sizeof(stale_posix_name), "/%s", stale_name);
      assert(shm_unlink(stale_posix_name) == 0);
   }

   /* INITIALIZING with no POSIX object is a distinct crash window: recovery
      must tolerate ENOENT, transition the record to READY, and report a normal
      not-found result for OpenFileMapping. A later creator must then succeed. */
   {
      char absent_name[96], absent_posix_name[100];
      snprintf(absent_name, sizeof(absent_name), "winapi_stale_absent_%ld", (long)getpid());
      pid_t child = fork();
      assert(child >= 0);
      if (child == 0) {
         execl(argv[0], argv[0], "--shmem-stale-init-child", absent_name, "no-object", (char *)NULL);
         _exit(55);
      }
      int status = 0;
      assert(waitpid(child, &status, 0) == child);
      assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
      assert(OpenFileMapping(FILE_MAP_READ, FALSE, absent_name) == NULL);
      assert(GetLastError() == ERROR_FILE_NOT_FOUND);
      HANDLE created_after_absence = CreateFileMapping(INVALID_HANDLE_VALUE, NULL,
         PAGE_READWRITE, 0, 4096, absent_name);
      assert(created_after_absence != NULL && GetLastError() == ERROR_SUCCESS);
      assert(CloseHandle(created_after_absence) == TRUE);
      snprintf(absent_posix_name, sizeof(absent_posix_name), "/%s", absent_name);
      assert(shm_unlink(absent_posix_name) == 0);
   }

   /* Multiple exec'd processes race to create the same named mapping. A pipe
      gate releases them together; the library's per-name interprocess lock
      must prevent any process from observing a partially initialized object. */
   {
      enum { SHMEM_PROCESS_CREATORS = 4 };
      char race_name[96];
      int gate[2];
      pid_t children[SHMEM_PROCESS_CREATORS];
      snprintf(race_name, sizeof(race_name), "winapi_process_race_%ld", (long)getpid());
      assert(pipe(gate) == 0);
      for (int i = 0; i < SHMEM_PROCESS_CREATORS; ++i) {
         children[i] = fork();
         assert(children[i] >= 0);
         if (children[i] == 0) {
            char token;
            close(gate[1]);
            if (read(gate[0], &token, 1) != 1) _exit(46);
            close(gate[0]);
            execl(argv[0], argv[0], "--shmem-create-race-child", race_name, (char *)NULL);
            _exit(47);
         }
      }
      close(gate[0]);
      for (int i = 0; i < SHMEM_PROCESS_CREATORS; ++i) assert(write(gate[1], "x", 1) == 1);
      close(gate[1]);
      for (int i = 0; i < SHMEM_PROCESS_CREATORS; ++i) {
         int status = 0;
         assert(waitpid(children[i], &status, 0) == children[i]);
         assert(WIFEXITED(status) && WEXITSTATUS(status) == 0);
      }
      HANDLE race_mapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, race_name);
      assert(race_mapping != NULL);
      unsigned char *race_view = (unsigned char *)MapViewOfFile(race_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      assert(race_view != NULL);
      assert(UnmapViewOfFile(race_view) == TRUE);
      assert(CloseHandle(race_mapping) == TRUE);
      char posix_race_name[100];
      snprintf(posix_race_name, sizeof(posix_race_name), "/%s", race_name);
      assert(shm_unlink(posix_race_name) == 0);
   }

   /* Read-only protection and handle rights must not allow writable views. */
   char readonly_name[96];
   snprintf(readonly_name, sizeof(readonly_name), "winapi_readonly_%ld", (long)getpid());
   HANDLE readonly_mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READONLY, 0, 4096, readonly_name);
   assert(readonly_mapping != NULL);
   SetLastError(ERROR_SUCCESS);
   HANDLE readonly_existing = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 8192, readonly_name);
   assert(readonly_existing != NULL && readonly_existing != readonly_mapping);
   assert(GetLastError() == ERROR_ALREADY_EXISTS);
   assert(MapViewOfFile(readonly_existing, FILE_MAP_WRITE, 0, 0, 4096) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   assert(CloseHandle(readonly_existing) == TRUE);
   unsigned char *readonly_view = (unsigned char *)MapViewOfFile(readonly_mapping, FILE_MAP_READ, 0, 0, 4096);
   assert(readonly_view != NULL);
   assert(MapViewOfFile(readonly_mapping, FILE_MAP_WRITE, 0, 0, 4096) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   assert(UnmapViewOfFile(readonly_view) == TRUE);
   assert(CloseHandle(readonly_mapping) == TRUE);
   HANDLE readonly_read_handle = OpenFileMapping(FILE_MAP_READ, FALSE, readonly_name);
   assert(readonly_read_handle != NULL);
   readonly_view = (unsigned char *)MapViewOfFile(readonly_read_handle, FILE_MAP_READ, 0, 0, 4096);
   assert(readonly_view != NULL);
   assert(MapViewOfFile(readonly_read_handle, FILE_MAP_ALL_ACCESS, 0, 0, 4096) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   assert(UnmapViewOfFile(readonly_view) == TRUE);
   assert(CloseHandle(readonly_read_handle) == TRUE);
   assert(OpenFileMapping(FILE_MAP_WRITE, FALSE, readonly_name) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   char readonly_posix_name[100];
   snprintf(readonly_posix_name, sizeof(readonly_posix_name), "/%s", readonly_name);
   assert(shm_unlink(readonly_posix_name) == 0);

   /* OpenFileMapping rights are per-handle, even when the underlying object
    * itself is writable. A read-only handle must not create writable views. */
   char restricted_name[96];
   snprintf(restricted_name, sizeof(restricted_name), "winapi_restricted_%ld", (long)getpid());
   HANDLE writable_object = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, restricted_name);
   assert(writable_object != NULL);
   HANDLE read_only_handle = OpenFileMapping(FILE_MAP_READ, FALSE, restricted_name);
   assert(read_only_handle != NULL);
   unsigned char *restricted_view = (unsigned char *)MapViewOfFile(read_only_handle, FILE_MAP_READ, 0, 0, 4096);
   assert(restricted_view != NULL);
   assert(MapViewOfFile(read_only_handle, FILE_MAP_WRITE, 0, 0, 4096) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   assert(MapViewOfFile(read_only_handle, FILE_MAP_ALL_ACCESS, 0, 0, 4096) == NULL);
   assert(GetLastError() == ERROR_ACCESS_DENIED);
   assert(UnmapViewOfFile(restricted_view) == TRUE);
   assert(CloseHandle(read_only_handle) == TRUE);
   assert(CloseHandle(writable_object) == TRUE);
   char restricted_posix_name[100];
   snprintf(restricted_posix_name, sizeof(restricted_posix_name), "/%s", restricted_name);
   assert(shm_unlink(restricted_posix_name) == 0);

   /* Unknown or unsupported rights are rejected rather than silently widened. */
   assert(OpenFileMapping(FILE_MAP_READ | FILE_MAP_COPY, FALSE, "winapi-no-such-mapping") == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);

   /* Unnamed mappings must work without leaving a discoverable POSIX name. */
   HANDLE unnamed_mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, NULL);
   assert(unnamed_mapping != NULL);
   unsigned char *unnamed_view = (unsigned char *)MapViewOfFile(unnamed_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
   assert(unnamed_view != NULL);
   unnamed_view[0] = 0x31;
   assert(unnamed_view[0] == 0x31);
   assert(UnmapViewOfFile(unnamed_view) == TRUE);
   assert(CloseHandle(unnamed_mapping) == TRUE);

   /* Concurrent same-process creation of one name must return independent
      handles to the same fixed-size object. The gate uses signaling, not polling. */
   char concurrent_name[96];
   snprintf(concurrent_name, sizeof(concurrent_name), "winapi_create_race_%ld", (long)getpid());
   enum { SHMEM_CREATE_THREADS = 6 };
   shmem_create_gate create_gate = { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0,
      SHMEM_CREATE_THREADS, 0 };
   pthread_t create_threads[SHMEM_CREATE_THREADS];
   shmem_create_race_args create_args[SHMEM_CREATE_THREADS];
   for (unsigned int i = 0; i < SHMEM_CREATE_THREADS; ++i) {
      create_args[i].gate = &create_gate;
      create_args[i].name = concurrent_name;
      create_args[i].mapping = NULL;
      create_args[i].last_error = ERROR_GEN_FAILURE;
      assert(pthread_create(&create_threads[i], NULL, shmem_create_race_worker, &create_args[i]) == 0);
   }
   unsigned int created_count = 0;
   unsigned int already_exists_count = 0;
   for (unsigned int i = 0; i < SHMEM_CREATE_THREADS; ++i) {
      assert(pthread_join(create_threads[i], NULL) == 0);
      assert(create_args[i].mapping != NULL);
      for (unsigned int j = 0; j < i; ++j)
         assert(create_args[i].mapping != create_args[j].mapping);
      if (create_args[i].last_error == ERROR_SUCCESS) ++created_count;
      else if (create_args[i].last_error == ERROR_ALREADY_EXISTS) ++already_exists_count;
      else assert(!"unexpected CreateFileMapping result in concurrent creation");
      unsigned char *concurrent_view = (unsigned char *)MapViewOfFile(create_args[i].mapping,
         FILE_MAP_READ, 0, 0, 4096);
      assert(concurrent_view != NULL);
      assert(UnmapViewOfFile(concurrent_view) == TRUE);
   }
   assert(created_count == 1);
   assert(already_exists_count == SHMEM_CREATE_THREADS - 1);
   for (unsigned int i = 0; i < SHMEM_CREATE_THREADS; ++i)
      assert(CloseHandle(create_args[i].mapping) == TRUE);
   assert(pthread_cond_destroy(&create_gate.cond) == 0);
   assert(pthread_mutex_destroy(&create_gate.mutex) == 0);
   char concurrent_posix_name[100];
   snprintf(concurrent_posix_name, sizeof(concurrent_posix_name), "/%s", concurrent_name);
   assert(shm_unlink(concurrent_posix_name) == 0);

   /* Shared-memory mapping bounds, access, and invalid-base handling. */
   char mapping_name[96];
   snprintf(mapping_name, sizeof(mapping_name), "winapi_smoke_%ld", (long)getpid());
   HANDLE mapping = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 4096, mapping_name);
   assert(mapping != NULL);
   /* Win32 returns the existing object unchanged when CreateFileMapping is
      called again with the same name, even if the requested size differs. */
   SetLastError(ERROR_SUCCESS);
   HANDLE mapping_larger = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 8192, mapping_name);
   assert(mapping_larger != NULL && mapping_larger != mapping);
   assert(GetLastError() == ERROR_ALREADY_EXISTS);
   assert(MapViewOfFile(mapping_larger, FILE_MAP_READ, 0, 4096, 1) == NULL);
   assert(CloseHandle(mapping_larger) == TRUE);
   SetLastError(ERROR_SUCCESS);
   HANDLE mapping_smaller = CreateFileMapping(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, 1024, mapping_name);
   assert(mapping_smaller != NULL && mapping_smaller != mapping);
   assert(GetLastError() == ERROR_ALREADY_EXISTS);
   assert(CloseHandle(mapping_smaller) == TRUE);

   /* MapViewOfFile validates the handle, access mask, alignment and bounds
      before entering mmap(2). These failures must not create a registered view. */
   SetLastError(ERROR_SUCCESS);
   assert(MapViewOfFile(NULL, FILE_MAP_READ, 0, 0, 1) == NULL);
   assert(GetLastError() == ERROR_INVALID_HANDLE);
   assert(MapViewOfFile(mapping, FILE_MAP_COPY, 0, 0, 1) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(MapViewOfFile(mapping, FILE_MAP_READ, 0, 1, 1) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(MapViewOfFile(mapping, FILE_MAP_READ, 0, 4096, 0) == NULL);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);
   assert(UnmapViewOfFile(NULL) == FALSE);
   assert(GetLastError() == ERROR_INVALID_PARAMETER);

   unsigned char *view = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
   assert(view != NULL);
   view[0] = 0x5a;
   view[4095] = 0xa5;
   assert(view[0] == 0x5a && view[4095] == 0xa5);
   /* Reopening a name must yield a distinct handle lifetime. */
   HANDLE mapping_again = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, mapping_name);
   assert(mapping_again != NULL && mapping_again != mapping);
   assert(CloseHandle(mapping_again) == TRUE);
   assert(MapViewOfFile(mapping, FILE_MAP_READ, 0, 4096, 1) == NULL);
   assert(CloseHandle(mapping) == FALSE); /* mapped views keep the handle busy */
   assert(GetLastError() == ERROR_BUSY);
   assert(view[0] == 0x5a); /* rejected close leaves the mapping usable */
   assert(UnmapViewOfFile(view) == TRUE);
   assert(UnmapViewOfFile(view) == FALSE);

   /* Concurrent view creation/destruction must serialize registry access and
      keep the mapping alive until every worker has unmapped its view. */
   view = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
   assert(view != NULL);
   enum { SHMEM_STRESS_THREADS = 4 };
   pthread_t shmem_workers[SHMEM_STRESS_THREADS];
   shmem_view_stress_args shmem_args[SHMEM_STRESS_THREADS];
   for (unsigned int i = 0; i < SHMEM_STRESS_THREADS; ++i) {
      shmem_args[i].mapping = mapping;
      shmem_args[i].iterations = 300;
      shmem_args[i].worker_id = i;
      assert(pthread_create(&shmem_workers[i], NULL, shmem_view_stress_worker, &shmem_args[i]) == 0);
   }
   shmem_close_race_args close_args;
   close_args.mapping = mapping;
   close_args.iterations = 1200;
   atomic_init(&close_args.attempts, 0u);
   pthread_t close_worker;
   assert(pthread_create(&close_worker, NULL, shmem_close_race_worker, &close_args) == 0);
   for (unsigned int i = 0; i < SHMEM_STRESS_THREADS; ++i)
      assert(pthread_join(shmem_workers[i], NULL) == 0);
   assert(pthread_join(close_worker, NULL) == 0);
   assert(atomic_load_explicit(&close_args.attempts, memory_order_relaxed) == close_args.iterations);
   assert(CloseHandle(mapping) == FALSE);
   assert(GetLastError() == ERROR_BUSY);
   assert(UnmapViewOfFile(view) == TRUE);

   /* A forked process drops its inherited local registry entry, then opens
      the named object independently and verifies cross-process visibility. */
   pid_t child = fork();
   assert(child >= 0);
   if (child == 0) {
      if (!CloseHandle(mapping)) _exit(10);
      HANDLE child_mapping = OpenFileMapping(FILE_MAP_ALL_ACCESS, FALSE, mapping_name);
      if (child_mapping == NULL) _exit(11);
      unsigned char *child_view = (unsigned char *)MapViewOfFile(child_mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
      if (child_view == NULL) _exit(12);
      child_view[17] = 0xC7;
      if (!UnmapViewOfFile(child_view)) _exit(13);
      if (!CloseHandle(child_mapping)) _exit(14);
      _exit(0);
   }
   int child_status = 0;
   assert(waitpid(child, &child_status, 0) == child);
   assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
   view = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, 4096);
   assert(view != NULL && view[17] == 0xC7);
   assert(UnmapViewOfFile(view) == TRUE);

   /* An independent exec'd process opens the named object without inheriting
      the parent's in-memory registries. It then exits with resources active. */
   pid_t exec_child = fork();
   assert(exec_child >= 0);
   if (exec_child == 0) {
      execl(argv[0], argv[0], "--shmem-crash-child", mapping_name, (char *)NULL);
      _exit(33);
   }
   int exec_status = 0;
   assert(waitpid(exec_child, &exec_status, 0) == exec_child);
   assert(WIFEXITED(exec_status) && WEXITSTATUS(exec_status) == 0);
   view = (unsigned char *)MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 4096);
   assert(view != NULL && view[18] == 0xD8);
   assert(UnmapViewOfFile(view) == TRUE);

   assert(CloseHandle(mapping) == TRUE);
   assert(CloseHandle(mapping) == FALSE);
   char shm_name[100];
   snprintf(shm_name, sizeof(shm_name), "/%s", mapping_name);
   assert(shm_unlink(shm_name) == 0);
#endif

   puts("OK");
   return 0;
}
