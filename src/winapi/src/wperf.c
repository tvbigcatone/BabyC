
#include "windows.h"

#if(ENABLE_WPERF == 1)

#include <sys/time.h>
#if _POSIX_C_SOURCE >= 199309L
   #include <time.h>   // for nanosleep
#else
   #ifdef HAVE_UNISTD_H
      #include <unistd.h> // for usleep
   #endif
#endif
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


/* PARAMS
 *  lpIdleTime   [out] Destination for idle time.
 *  lpKernelTime [out] Destination for kernel time.
 *  lpUserTime   [out] Destination for user time.
 *
 * RETURNS
 *  TRUE if success, FALSE otherwise.
 */
static int cpu_ticks_to_filetime_ticks(uint64_t ticks, long ticks_per_second,
                                       uint64_t *result)
{
   uint64_t hz, whole, remainder, fractional, base;
   if (result == NULL || ticks_per_second <= 0) return 0;
   hz = (uint64_t)ticks_per_second;
   whole = ticks / hz;
   remainder = ticks % hz;
   if (whole > UINT64_MAX / UINT64_C(10000000) ||
       remainder > UINT64_MAX / UINT64_C(10000000))
      return 0;
   base = whole * UINT64_C(10000000);
   fractional = (remainder * UINT64_C(10000000)) / hz;
   if (base > UINT64_MAX - fractional) return 0;
   *result = base + fractional;
   return 1;
}

static int add_cpu_ticks(uint64_t *total, uint64_t value)
{
   if (total == NULL || *total > UINT64_MAX - value) return 0;
   *total += value;
   return 1;
}

static void cpu_ticks_to_filetime(uint64_t ticks, FILETIME *output)
{
   output->dwLowDateTime = (DWORD)(ticks & UINT64_C(0xffffffff));
   output->dwHighDateTime = (DWORD)(ticks >> 32);
}

/* Linux exposes aggregate CPU counters in /proc/stat in USER_HZ units.
 * Convert those aggregate counters directly to FILETIME's 100 ns units.
 * Dividing by CPU count or using floating-point fractions loses time and
 * produces incorrect high/low words. */
BOOL WINAPI GetSystemTimes(LPFILETIME lpIdleTime, LPFILETIME lpKernelTime,
                           LPFILETIME lpUserTime)
{
   FILE *procfile;
   int successful_values;
   long clocks_per_second;
   char line[512];
   unsigned long long cpu_user = 0, cpu_nice = 0, cpu_sys = 0, cpu_idle = 0;
   unsigned long long cpu_iowait = 0, cpu_hardirq = 0, cpu_softirq = 0, cpu_steal = 0;
   uint64_t idle_ticks, kernel_ticks = 0, user_ticks = 0;
   uint64_t idle_filetime, kernel_filetime, user_filetime;
   FILETIME idle_result, kernel_result, user_result;

   clocks_per_second = sysconf(_SC_CLK_TCK);
   if (clocks_per_second <= 0) {
      SetLastError(ERROR_GEN_FAILURE);
      return FALSE;
   }
   procfile = fopen("/proc/stat", "r");
   if (procfile == NULL) {
      SetLastError(errno == EACCES ? ERROR_ACCESS_DENIED : ERROR_NOT_SUPPORTED);
      return FALSE;
   }
   if (fgets(line, sizeof(line), procfile) == NULL) {
      int saved = ferror(procfile) ? errno : 0;
      fclose(procfile);
      SetLastError(saved == EACCES ? ERROR_ACCESS_DENIED : ERROR_BAD_FORMAT);
      return FALSE;
   }
   if (fclose(procfile) != 0) {
      SetLastError(ERROR_GEN_FAILURE);
      return FALSE;
   }
   if (strncmp(line, "cpu ", 4) != 0) {
      SetLastError(ERROR_BAD_FORMAT);
      return FALSE;
   }

   successful_values = sscanf(line + 4,
      "%llu %llu %llu %llu %llu %llu %llu %llu",
      &cpu_user, &cpu_nice, &cpu_sys, &cpu_idle,
      &cpu_iowait, &cpu_hardirq, &cpu_softirq, &cpu_steal);
   if (successful_values < 4) {
      SetLastError(ERROR_BAD_FORMAT);
      return FALSE;
   }
   /* Newer counters may be absent on older kernels; their initialized zero
    * values are valid. The first four counters are mandatory. */
   idle_ticks = (uint64_t)cpu_idle;
   /* Win32's kernel time includes the idle thread time. */
   kernel_ticks = idle_ticks;
   if (!add_cpu_ticks(&kernel_ticks, (uint64_t)cpu_sys) ||
       !add_cpu_ticks(&kernel_ticks, (uint64_t)cpu_iowait) ||
       !add_cpu_ticks(&kernel_ticks, (uint64_t)cpu_hardirq) ||
       !add_cpu_ticks(&kernel_ticks, (uint64_t)cpu_softirq) ||
       !add_cpu_ticks(&kernel_ticks, (uint64_t)cpu_steal) ||
       !add_cpu_ticks(&user_ticks, (uint64_t)cpu_user) ||
       !add_cpu_ticks(&user_ticks, (uint64_t)cpu_nice)) {
      SetLastError(ERROR_ARITHMETIC_OVERFLOW);
      return FALSE;
   }
   if (!cpu_ticks_to_filetime_ticks(idle_ticks, clocks_per_second, &idle_filetime) ||
       !cpu_ticks_to_filetime_ticks(kernel_ticks, clocks_per_second, &kernel_filetime) ||
       !cpu_ticks_to_filetime_ticks(user_ticks, clocks_per_second, &user_filetime)) {
      SetLastError(ERROR_ARITHMETIC_OVERFLOW);
      return FALSE;
   }
   cpu_ticks_to_filetime(idle_filetime, &idle_result);
   cpu_ticks_to_filetime(kernel_filetime, &kernel_result);
   cpu_ticks_to_filetime(user_filetime, &user_result);

   /* Do not publish a partial result if parsing/conversion failed. */
   if (lpIdleTime != NULL) *lpIdleTime = idle_result;
   if (lpKernelTime != NULL) *lpKernelTime = kernel_result;
   if (lpUserTime != NULL) *lpUserTime = user_result;
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

/* Helpful conversion constants. */
// static int64_t usec_per_sec = 1000000;
// static int64_t usec_per_msec = 1000;
#define USEC_PER_SEC    1000000

BOOL WINAPI QueryPerformanceCounter(
  _Out_ LARGE_INTEGER *lpPerformanceCount
)
{
   struct timeval time;

   /* Sanity check. */
   assert(lpPerformanceCount != NULL);

   /* Grab the current time. */
   gettimeofday(&time, NULL);
   (*lpPerformanceCount).QuadPart = time.tv_usec + /* Microseconds. */
                                    time.tv_sec * USEC_PER_SEC; /* Seconds. */
   return TRUE;
}

BOOL WINAPI QueryPerformanceFrequency(
  _Out_ LARGE_INTEGER *lpFrequency
)
{
   /* Sanity check. */
   assert(lpFrequency != NULL);

   /* gettimeofday reports to microsecond accuracy. */
   (*lpFrequency).QuadPart = USEC_PER_SEC;
   return TRUE;
}

VOID WINAPI Sleep(
  _In_ DWORD dwMilliseconds
)
{
#if _POSIX_C_SOURCE >= 199309L
    struct timespec ts;
    ts.tv_sec = dwMilliseconds / 1000;
    ts.tv_nsec = (dwMilliseconds % 1000) * 1000000;
    nanosleep(&ts, NULL);
#else
    usleep(dwMilliseconds * 1000);
#endif
}

#endif