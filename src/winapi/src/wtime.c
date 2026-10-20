#include "windows.h"
#include "wprivate.h"

#include <stdint.h>
#include <time.h>

#define WINAPI_FILETIME_EPOCH_DIFF INT64_C(11644473600)
#define WINAPI_TICKS_PER_SECOND UINT64_C(10000000)
#define WINAPI_TICKS_PER_MILLISECOND UINT64_C(10000)
#define WINAPI_SECONDS_PER_DAY INT64_C(86400)
#define WINAPI_TICKS_PER_DAY (WINAPI_TICKS_PER_SECOND * UINT64_C(86400))
#define WINAPI_NANOSECONDS_PER_SECOND INT64_C(1000000000)

/* Gregorian calendar arithmetic, with days measured from 1970-01-01. */
static int64_t days_from_civil(int64_t year, unsigned int month, unsigned int day)
{
   int64_t era;
   int64_t year_of_era;
   int month_prime;
   int64_t day_of_year;
   int64_t day_of_era;

   year -= month <= 2u;
   era = (year >= 0 ? year : year - 399) / 400;
   year_of_era = year - era * 400;
   month_prime = (int)month + (month > 2u ? -3 : 9);
   day_of_year = (153 * month_prime + 2) / 5 + (int64_t)day - 1;
   day_of_era = year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
   return era * 146097 + day_of_era - 719468;
}

static void civil_from_days(int64_t days, int64_t *year_out,
                            unsigned int *month_out, unsigned int *day_out)
{
   int64_t z = days + 719468;
   int64_t era = (z >= 0 ? z : z - 146096) / 146097;
   int64_t day_of_era = z - era * 146097;
   int64_t year_of_era = (day_of_era - day_of_era / 1460 +
      day_of_era / 36524 - day_of_era / 146096) / 365;
   int64_t year = year_of_era + era * 400;
   int64_t day_of_year = day_of_era -
      (365 * year_of_era + year_of_era / 4 - year_of_era / 100);
   int64_t month_prime = (5 * day_of_year + 2) / 153;
   unsigned int day = (unsigned int)(day_of_year - (153 * month_prime + 2) / 5 + 1);
   unsigned int month = (unsigned int)(month_prime + (month_prime < 10 ? 3 : -9));

   year += month <= 2u;
   *year_out = year;
   *month_out = month;
   *day_out = day;
}

static int is_leap_year(unsigned int year)
{
   return (year % 4u == 0u) && ((year % 100u != 0u) || (year % 400u == 0u));
}

static unsigned int days_in_month(unsigned int year, unsigned int month)
{
   static const unsigned char month_days[12] = {
      31u, 28u, 31u, 30u, 31u, 30u, 31u, 31u, 30u, 31u, 30u, 31u
   };
   if (month < 1u || month > 12u) return 0u;
   if (month == 2u && is_leap_year(year)) return 29u;
   return month_days[month - 1u];
}

static uint64_t filetime_ticks(const FILETIME *file_time)
{
   return ((uint64_t)file_time->dwHighDateTime << 32) |
      (uint64_t)file_time->dwLowDateTime;
}

static void ticks_to_filetime(uint64_t ticks, FILETIME *file_time)
{
   file_time->dwLowDateTime = (DWORD)(ticks & UINT64_C(0xffffffff));
   file_time->dwHighDateTime = (DWORD)(ticks >> 32);
}

/* Shared conversion for POSIX stat timestamps. Sub-100ns precision is floored. */
int winapi_timespec_to_filetime(const struct timespec *ts, FILETIME *file_time)
{
   int64_t seconds;
   uint64_t ticks;
   uint64_t subsecond_ticks;

   if (ts == NULL || file_time == NULL ||
       ts->tv_nsec < 0 || ts->tv_nsec >= (long)WINAPI_NANOSECONDS_PER_SECOND)
      return 0;
   seconds = (int64_t)ts->tv_sec;
   if ((time_t)seconds != ts->tv_sec || seconds < -WINAPI_FILETIME_EPOCH_DIFF ||
       seconds > INT64_MAX - WINAPI_FILETIME_EPOCH_DIFF)
      return 0;
   seconds += WINAPI_FILETIME_EPOCH_DIFF;
   if (seconds < 0 || (uint64_t)seconds > UINT64_MAX / WINAPI_TICKS_PER_SECOND)
      return 0;
   ticks = (uint64_t)seconds * WINAPI_TICKS_PER_SECOND;
   subsecond_ticks = (uint64_t)ts->tv_nsec / UINT64_C(100);
   if (ticks > UINT64_MAX - subsecond_ticks) return 0;
   ticks_to_filetime(ticks + subsecond_ticks, file_time);
   return 1;
}

static int current_timezone_offset_seconds(int64_t *offset_out)
{
   time_t now;
   struct tm local_tm;
   struct tm utc_tm;
   int64_t local_seconds;
   int64_t utc_seconds;

   if (offset_out == NULL) return 0;
   now = time(NULL);
   if (now == (time_t)-1 || localtime_r(&now, &local_tm) == NULL ||
       gmtime_r(&now, &utc_tm) == NULL)
      return 0;
   local_seconds = days_from_civil((int64_t)local_tm.tm_year + 1900,
      (unsigned int)local_tm.tm_mon + 1u, (unsigned int)local_tm.tm_mday) * WINAPI_SECONDS_PER_DAY +
      (int64_t)local_tm.tm_hour * 3600 + (int64_t)local_tm.tm_min * 60 + local_tm.tm_sec;
   utc_seconds = days_from_civil((int64_t)utc_tm.tm_year + 1900,
      (unsigned int)utc_tm.tm_mon + 1u, (unsigned int)utc_tm.tm_mday) * WINAPI_SECONDS_PER_DAY +
      (int64_t)utc_tm.tm_hour * 3600 + (int64_t)utc_tm.tm_min * 60 + utc_tm.tm_sec;
   *offset_out = local_seconds - utc_seconds;
   return 1;
}

/* These APIs intentionally use the bias in force now, matching the Win32
 * FileTimeToLocalFileTime/LocalFileTimeToFileTime current-zone policy rather
 * than re-evaluating DST for the date encoded by the FILETIME. */
static BOOL shift_filetime_by_current_bias(const FILETIME *input,
                                          FILETIME *output, int to_local)
{
   int64_t offset_seconds;
   int64_t signed_offset;
   uint64_t magnitude_ticks;
   uint64_t ticks;

   if (input == NULL || output == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (!current_timezone_offset_seconds(&offset_seconds)) {
      SetLastError(ERROR_GEN_FAILURE);
      return FALSE;
   }
   signed_offset = to_local ? offset_seconds : -offset_seconds;
   ticks = filetime_ticks(input);
   magnitude_ticks = (uint64_t)(signed_offset < 0 ? -signed_offset : signed_offset) *
      WINAPI_TICKS_PER_SECOND;
   if (signed_offset >= 0) {
      if (ticks > UINT64_MAX - magnitude_ticks) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
      }
      ticks += magnitude_ticks;
   } else {
      if (ticks < magnitude_ticks) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
      }
      ticks -= magnitude_ticks;
   }
   ticks_to_filetime(ticks, output);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

static void fill_systemtime(LPSYSTEMTIME st, const struct tm *tm, long nsec)
{
   st->wYear         = (WORD)(tm->tm_year + 1900);
   st->wMonth        = (WORD)(tm->tm_mon + 1);
   st->wDayOfWeek    = (WORD)tm->tm_wday;
   st->wDay          = (WORD)tm->tm_mday;
   st->wHour         = (WORD)tm->tm_hour;
   st->wMinute       = (WORD)tm->tm_min;
   st->wSecond       = (WORD)tm->tm_sec;
   st->wMilliseconds = (WORD)(nsec / 1000000L); /* Windows tronca, non arrotonda */
}

void WINAPI GetSystemTime(LPSYSTEMTIME lpSystemTime)
{
   struct timespec ts;
   struct tm tm;
   if (lpSystemTime == NULL) return;
   if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || gmtime_r(&ts.tv_sec, &tm) == NULL) {
      SetLastError(ERROR_GEN_FAILURE);
      return;
   }
   fill_systemtime(lpSystemTime, &tm, ts.tv_nsec);
}

void WINAPI GetLocalTime(LPSYSTEMTIME lpSystemTime)
{
   struct timespec ts;
   struct tm tm;
   if (lpSystemTime == NULL) return;
   if (clock_gettime(CLOCK_REALTIME, &ts) != 0 || localtime_r(&ts.tv_sec, &tm) == NULL) {
      SetLastError(ERROR_GEN_FAILURE);
      return;
   }
   fill_systemtime(lpSystemTime, &tm, ts.tv_nsec);
}

void WINAPI GetSystemTimeAsFileTime(LPFILETIME lpSystemTimeAsFileTime)
{
   struct timespec ts;
   if (lpSystemTimeAsFileTime == NULL) return;
   if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
      SetLastError(ERROR_GEN_FAILURE);
      return;
   }
   if (!winapi_timespec_to_filetime(&ts, lpSystemTimeAsFileTime)) {
      SetLastError(ERROR_INVALID_DATA);
      return;
   }
   SetLastError(ERROR_SUCCESS);
}

BOOL WINAPI SystemTimeToFileTime(const SYSTEMTIME *lpSystemTime, LPFILETIME lpFileTime)
{
   int64_t days;
   int64_t seconds;
   uint64_t ticks;
   unsigned int year, month, day;

   if (lpSystemTime == NULL || lpFileTime == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   year = lpSystemTime->wYear;
   month = lpSystemTime->wMonth;
   day = lpSystemTime->wDay;
   if (year < 1601u || year > 30827u || month < 1u || month > 12u ||
       day < 1u || day > days_in_month(year, month) ||
       lpSystemTime->wHour > 23u || lpSystemTime->wMinute > 59u ||
       lpSystemTime->wSecond > 59u || lpSystemTime->wMilliseconds > 999u) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   days = days_from_civil(year, month, day) - days_from_civil(1601, 1u, 1u);
   seconds = days * WINAPI_SECONDS_PER_DAY + (int64_t)lpSystemTime->wHour * 3600 +
      (int64_t)lpSystemTime->wMinute * 60 + lpSystemTime->wSecond;
   if (seconds < 0 || (uint64_t)seconds > UINT64_MAX / WINAPI_TICKS_PER_SECOND) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   ticks = (uint64_t)seconds * WINAPI_TICKS_PER_SECOND;
   {
      uint64_t fractional_ticks = (uint64_t)lpSystemTime->wMilliseconds * WINAPI_TICKS_PER_MILLISECOND;
      if (ticks > UINT64_MAX - fractional_ticks) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
      }
      ticks += fractional_ticks;
   }
   if (ticks > UINT64_C(0x7fffffffffffffff)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   ticks_to_filetime(ticks, lpFileTime);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

BOOL WINAPI FileTimeToSystemTime(const FILETIME *lpFileTime, LPSYSTEMTIME lpSystemTime)
{
   uint64_t ticks;
   uint64_t days;
   uint64_t ticks_in_day;
   int64_t year;
   unsigned int month, day;
   int64_t days_since_unix;

   if (lpFileTime == NULL || lpSystemTime == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   ticks = filetime_ticks(lpFileTime);
   if (ticks > UINT64_C(0x7fffffffffffffff)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   days = ticks / WINAPI_TICKS_PER_DAY;
   ticks_in_day = ticks % WINAPI_TICKS_PER_DAY;
   days_since_unix = days_from_civil(1601, 1u, 1u) + (int64_t)days;
   civil_from_days(days_since_unix, &year, &month, &day);
   if (year < 1601 || year > 65535) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   lpSystemTime->wYear = (WORD)year;
   lpSystemTime->wMonth = (WORD)month;
   lpSystemTime->wDay = (WORD)day;
   lpSystemTime->wDayOfWeek = (WORD)((days + 1u) % 7u); /* 1601-01-01 was Monday */
   lpSystemTime->wHour = (WORD)(ticks_in_day / (WINAPI_TICKS_PER_SECOND * UINT64_C(3600)));
   ticks_in_day %= WINAPI_TICKS_PER_SECOND * UINT64_C(3600);
   lpSystemTime->wMinute = (WORD)(ticks_in_day / (WINAPI_TICKS_PER_SECOND * UINT64_C(60)));
   ticks_in_day %= WINAPI_TICKS_PER_SECOND * UINT64_C(60);
   lpSystemTime->wSecond = (WORD)(ticks_in_day / WINAPI_TICKS_PER_SECOND);
   ticks_in_day %= WINAPI_TICKS_PER_SECOND;
   lpSystemTime->wMilliseconds = (WORD)(ticks_in_day / WINAPI_TICKS_PER_MILLISECOND);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

LONG WINAPI CompareFileTime(const FILETIME *lpFileTime1, const FILETIME *lpFileTime2)
{
   uint64_t a, b;
   if (lpFileTime1 == NULL || lpFileTime2 == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
   }
   a = filetime_ticks(lpFileTime1);
   b = filetime_ticks(lpFileTime2);
   SetLastError(ERROR_SUCCESS);
   return a < b ? -1L : (a > b ? 1L : 0L);
}

BOOL WINAPI FileTimeToLocalFileTime(const FILETIME *lpFileTime, LPFILETIME lpLocalFileTime)
{
   return shift_filetime_by_current_bias(lpFileTime, lpLocalFileTime, 1);
}

BOOL WINAPI LocalFileTimeToFileTime(const FILETIME *lpLocalFileTime, LPFILETIME lpFileTime)
{
   return shift_filetime_by_current_bias(lpLocalFileTime, lpFileTime, 0);
}

/* Changing the system clock requires privileges; this backend does not do it. */
BOOL WINAPI SetLocalTime(const SYSTEMTIME *lpSystemTime)
{
   (void)lpSystemTime;
   SetLastError(ERROR_ACCESS_DENIED);
   return FALSE;
}
