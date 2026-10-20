#include "windows.h"

#if(ENABLE_WSTATS == 1)

#include <sys/sysinfo.h>
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include <stdint.h>
#include <unistd.h>

/* MemAvailable (kernel >= 3.14) is the best available estimate of reusable RAM. */
static int multiply_ull(unsigned long long a, unsigned long long b,
                        unsigned long long *out)
{
   if (out == NULL || (b != 0 && a > ULLONG_MAX / b)) return 0;
   *out = a * b;
   return 1;
}

static int add_ull(unsigned long long a, unsigned long long b,
                   unsigned long long *out)
{
   if (out == NULL || a > ULLONG_MAX - b) return 0;
   *out = a + b;
   return 1;
}

static int read_mem_available(unsigned long long *bytes)
{
   FILE *f = fopen("/proc/meminfo", "r");
   char line[128];
   int found = 0;

   if (f == NULL || bytes == NULL) {
      if (f != NULL) fclose(f);
      return 0;
   }
   while (fgets(line, sizeof(line), f)) {
      unsigned long long kb, value;
      if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1 &&
          multiply_ull(kb, 1024ULL, &value)) {
         *bytes = value;
         found = 1;
         break;
      }
   }
   fclose(f);
   return found;
}

static unsigned long long read_process_virtual_bytes(void)
{
   FILE *f = fopen("/proc/self/statm", "r");
   unsigned long long pages = 0, bytes = 0;
   long page_size = sysconf(_SC_PAGESIZE);
   if (f == NULL) return 0;
   if (fscanf(f, "%llu", &pages) != 1 || page_size <= 0 ||
       !multiply_ull(pages, (unsigned long long)page_size, &bytes))
      bytes = 0;
   fclose(f);
   return bytes;
}

BOOL WINAPI GlobalMemoryStatusEx(
  _Inout_ LPMEMORYSTATUSEX lpBuffer
)
{
   struct sysinfo si;
   unsigned long long total, avail, swap_total, swap_free;
   unsigned long long raw_avail, total_page, avail_page;

   if (lpBuffer == NULL || lpBuffer->dwLength != sizeof(MEMORYSTATUSEX)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (sysinfo(&si) != 0) {
      SetLastError(ERROR_GEN_FAILURE);
      return FALSE;
   }

   if (!multiply_ull((unsigned long long)si.totalram, si.mem_unit, &total) ||
       !multiply_ull((unsigned long long)si.totalswap, si.mem_unit, &swap_total) ||
       !multiply_ull((unsigned long long)si.freeswap, si.mem_unit, &swap_free)) {
      SetLastError(ERROR_ARITHMETIC_OVERFLOW);
      return FALSE;
   }
   if (!read_mem_available(&avail)) {
      if (!add_ull((unsigned long long)si.freeram,
                   (unsigned long long)si.bufferram, &raw_avail) ||
          !multiply_ull(raw_avail, si.mem_unit, &avail)) {
         SetLastError(ERROR_ARITHMETIC_OVERFLOW);
         return FALSE;
      }
   }
   /* Kernel estimates can briefly exceed the reported total; never underflow
    * memory-load arithmetic or advertise more available physical RAM than total. */
   if (avail > total) avail = total;
   if (!add_ull(total, swap_total, &total_page) ||
       !add_ull(avail, swap_free, &avail_page)) {
      SetLastError(ERROR_ARITHMETIC_OVERFLOW);
      return FALSE;
   }
   if (avail_page > total_page) avail_page = total_page;

   memset(lpBuffer, 0, sizeof(*lpBuffer));
   lpBuffer->dwLength = sizeof(MEMORYSTATUSEX);
   lpBuffer->ullTotalPhys = total;
   lpBuffer->ullAvailPhys = avail;
   /* Use division in floating point to avoid overflow in (total-avail)*100. */
   lpBuffer->dwMemoryLoad = total == 0 ? 0 :
      (DWORD)(((long double)(total - avail) * 100.0L) / (long double)total);
   if (lpBuffer->dwMemoryLoad > 100u) lpBuffer->dwMemoryLoad = 100u;
   /* Page file = RAM + swap: this is an estimate of the host commit limit. */
   lpBuffer->ullTotalPageFile = total_page;
   lpBuffer->ullAvailPageFile = avail_page;
   /* Linux does not expose a direct Windows-equivalent per-process VA limit.
    * Use a conservative architecture ceiling and subtract this process's VmSize. */
   lpBuffer->ullTotalVirtual = (sizeof(void *) == 8) ? (1ULL << 47) : (3ULL << 30);
   {
      unsigned long long used_virtual = read_process_virtual_bytes();
      lpBuffer->ullAvailVirtual = used_virtual < lpBuffer->ullTotalVirtual ?
         lpBuffer->ullTotalVirtual - used_virtual : 0;
   }
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

#endif
