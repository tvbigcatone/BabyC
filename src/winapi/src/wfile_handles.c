#ifndef _GNU_SOURCE
#define _GNU_SOURCE 1
#endif
#include "windows.h"
#include "wprivate.h"

#include <stdatomic.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <time.h>
#include <limits.h>

struct file_lock_range {
   struct file_lock_range *next;
   uint64_t offset;
   uint64_t length;
};

struct file_handle_entry {
   TAILQ_ENTRY(file_handle_entry) entries;
   HANDLE handle;
   int fd;
   int readable;
   int writable;
   unsigned int active_ops;
   int closing;
   struct file_lock_range *locks;
   pthread_mutex_t io_lock; /* serialize operations using the shared file offset */
};
TAILQ_HEAD(file_handle_list, file_handle_entry);
static struct file_handle_list g_file_handles = TAILQ_HEAD_INITIALIZER(g_file_handles);
static pthread_mutex_t g_file_handles_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_file_handles_idle = PTHREAD_COND_INITIALIZER;

static DWORD error_from_errno(int e)
{
   switch (e) {
      case ENOENT: return ERROR_FILE_NOT_FOUND;
      case ENOTDIR: return ERROR_PATH_NOT_FOUND;
      case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
      case EEXIST: return ERROR_FILE_EXISTS;
      case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
      case EMFILE: case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
      case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
      case ENOSPC: case EFBIG: return ERROR_DISK_FULL;
      case EBADF: return ERROR_INVALID_HANDLE;
      case EISDIR: return ERROR_ACCESS_DENIED;
      case EINVAL: return ERROR_INVALID_PARAMETER;
#ifdef EOVERFLOW
      case EOVERFLOW: return ERROR_ARITHMETIC_OVERFLOW;
#endif
#ifdef ENOSYS
      case ENOSYS: return ERROR_NOT_SUPPORTED;
#endif
#ifdef ENOTSUP
      case ENOTSUP: return ERROR_NOT_SUPPORTED;
#endif
      default: return ERROR_GEN_FAILURE;
   }
}

static struct file_handle_entry *find_file_handle_locked(HANDLE handle)
{
   struct file_handle_entry *entry;
   TAILQ_FOREACH(entry, &g_file_handles, entries)
      if (entry->handle == handle) return entry;
   return NULL;
}

HANDLE WINAPI CreateFile(LPCTSTR lpFileName, DWORD dwDesiredAccess,
   DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes,
   DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile)
{
   int flags = O_CLOEXEC;
   int readable = 0, writable = 0, fd, existed = 0;
   char *converted = NULL;
   const char *path;
   struct stat st;
   struct file_handle_entry *entry;
   HANDLE token;

   if (!winapi_inited()) return INVALID_HANDLE_VALUE;
   if (lpFileName == NULL || lpFileName[0] == '\0') {
      SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE;
   }
   if (lpSecurityAttributes != NULL || hTemplateFile != NULL) {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return INVALID_HANDLE_VALUE;
   }
   /* Share-mode enforcement is not implemented yet. Reject it explicitly so
    * callers are not led to believe Win32 sharing guarantees are provided. */
   if (dwShareMode != 0) {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return INVALID_HANDLE_VALUE;
   }
   if ((dwDesiredAccess & ~(GENERIC_READ | GENERIC_WRITE)) != 0 || dwDesiredAccess == 0) {
      SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE;
   }
   if (dwCreationDisposition < CREATE_NEW || dwCreationDisposition > TRUNCATE_EXISTING) {
      SetLastError(ERROR_INVALID_PARAMETER); return INVALID_HANDLE_VALUE;
   }
   /* The first increment covers ordinary file attributes; special flags and
    * Windows-only attributes must not be silently treated as implemented. */
   if (dwFlagsAndAttributes != 0 && dwFlagsAndAttributes != FILE_ATTRIBUTE_NORMAL) {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return INVALID_HANDLE_VALUE;
   }

   readable = (dwDesiredAccess & GENERIC_READ) != 0;
   writable = (dwDesiredAccess & GENERIC_WRITE) != 0;
   if (readable && writable) flags |= O_RDWR;
   else if (writable) flags |= O_WRONLY;
   else flags |= O_RDONLY;

   path = lpFileName;
#ifndef _UNICODE
   /* Avoid probing mounted drives for ordinary POSIX paths: is_win_path()
    * enumerates mount points and is unnecessary for names with no Windows
    * separator or drive prefix. */
   if (strchr(path, '\\') != NULL ||
       (path[0] != '\0' && path[1] == ':')) {
      converted = win2posix_path(path);
      if (converted == NULL) { SetLastError(error_from_errno(errno)); return INVALID_HANDLE_VALUE; }
      path = converted;
   }
#else
   /* Wide-character path conversion is not yet implemented. */
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return INVALID_HANDLE_VALUE;
#endif

   switch (dwCreationDisposition) {
      case CREATE_NEW: flags |= O_CREAT | O_EXCL; break;
      case CREATE_ALWAYS:
         /* O_TRUNC combined with O_RDONLY is unspecified by POSIX and Linux
          * may truncate anyway. Win32 requires write access for truncation. */
         if (!writable) { free(converted); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
         flags |= O_CREAT | O_TRUNC;
         break;
      case OPEN_EXISTING: break;
      case OPEN_ALWAYS: flags |= O_CREAT; break;
      case TRUNCATE_EXISTING:
         if (!writable) { free(converted); SetLastError(ERROR_ACCESS_DENIED); return INVALID_HANDLE_VALUE; }
         flags |= O_TRUNC; break;
   }
   /* Determine CREATE_ALWAYS / OPEN_ALWAYS atomically. A separate stat()
    * followed by open() races with other processes and can report the wrong
    * ERROR_ALREADY_EXISTS status. Try exclusive creation first; if the name
    * already exists, perform the requested open/truncate. If the object is
    * removed between those operations, retry instead of leaking ENOENT from
    * an otherwise valid CREATE_ALWAYS / OPEN_ALWAYS request. */
   if (dwCreationDisposition == CREATE_ALWAYS ||
       dwCreationDisposition == OPEN_ALWAYS) {
      int create_flags = (flags & ~O_TRUNC) | O_CREAT | O_EXCL;
      for (;;) {
         fd = open(path, create_flags, 0666);
         if (fd >= 0) {
            existed = 0;
            break;
         }
         if (errno != EEXIST) break;
         existed = 1;
         fd = open(path, flags, 0666);
         if (fd >= 0 || errno != ENOENT) break;
      }
   } else {
      fd = open(path, flags, 0666);
   }
   {
      int saved = errno;
      free(converted);
      if (fd < 0) { SetLastError(error_from_errno(saved)); return INVALID_HANDLE_VALUE; }
   }
   if (fstat(fd, &st) != 0) {
      int saved = errno;
      close(fd);
      SetLastError(error_from_errno(saved));
      return INVALID_HANDLE_VALUE;
   }
   if (!S_ISREG(st.st_mode)) {
      close(fd);
      SetLastError(ERROR_ACCESS_DENIED);
      return INVALID_HANDLE_VALUE;
   }

   entry = calloc(1, sizeof(*entry));
   if (entry == NULL) {
      close(fd); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE;
   }
   token = winapi_allocate_handle_token();
   if (token == NULL) {
      free(entry); close(fd); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE;
   }
   if (pthread_mutex_init(&entry->io_lock, NULL) != 0) {
      free(entry); close(fd); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return INVALID_HANDLE_VALUE;
   }
   entry->handle = token; entry->fd = fd;
   entry->readable = readable; entry->writable = writable;
   pthread_mutex_lock(&g_file_handles_lock);
   TAILQ_INSERT_TAIL(&g_file_handles, entry, entries);
   pthread_mutex_unlock(&g_file_handles_lock);
   SetLastError(existed ? ERROR_ALREADY_EXISTS : ERROR_SUCCESS);
   return token;
}

/* Acquire a short-lived operation lease.  The entry remains allocated until
 * all leases have ended, even if CloseHandle concurrently removes it. */
int wfile_begin_io(HANDLE handle, int required_access, int *fd_out, void **lease_out)
{
   struct file_handle_entry *entry;
   if (fd_out == NULL || lease_out == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return 0;
   }
   *fd_out = -1;
   *lease_out = NULL;
   pthread_mutex_lock(&g_file_handles_lock);
   entry = find_file_handle_locked(handle);
   if (entry == NULL || entry->closing) {
      pthread_mutex_unlock(&g_file_handles_lock);
      SetLastError(ERROR_INVALID_HANDLE);
      return 0;
   }
   if (((required_access & 1) != 0 && !entry->readable) ||
       ((required_access & 2) != 0 && !entry->writable)) {
      pthread_mutex_unlock(&g_file_handles_lock);
      SetLastError(ERROR_ACCESS_DENIED);
      return 0;
   }
   ++entry->active_ops;
   *fd_out = entry->fd;
   *lease_out = entry;
   pthread_mutex_unlock(&g_file_handles_lock);
   /* active_ops pins the entry while we wait for this per-handle lock. */
   pthread_mutex_lock(&entry->io_lock);
   return 1;
}

void wfile_end_io(void *lease)
{
   struct file_handle_entry *entry = (struct file_handle_entry *)lease;
   if (entry == NULL) return;
   pthread_mutex_unlock(&entry->io_lock);
   pthread_mutex_lock(&g_file_handles_lock);
   if (entry->active_ops > 0) --entry->active_ops;
   if (entry->active_ops == 0) pthread_cond_broadcast(&g_file_handles_idle);
   pthread_mutex_unlock(&g_file_handles_lock);
}

/* Duplicate a registered file handle's descriptor while an operation lease
 * pins it. The duplicate has an independent lifetime, so a mapping remains
 * valid after the original WinAPI file handle is closed. required_access uses
 * bit 0 for read and bit 1 for write. */
int wfile_duplicate_fd(HANDLE handle, int required_access, int *fd_out,
                       int *readable_out, int *writable_out)
{
   int fd = -1, duplicate = -1;
   void *lease = NULL;
   if (fd_out == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
   *fd_out = -1;
   if (!wfile_begin_io(handle, required_access, &fd, &lease)) return 0;
#ifdef F_DUPFD_CLOEXEC
   duplicate = fcntl(fd, F_DUPFD_CLOEXEC, 0);
#else
   duplicate = dup(fd);
   if (duplicate >= 0 && fcntl(duplicate, F_SETFD, FD_CLOEXEC) < 0) {
      int saved = errno; close(duplicate); duplicate = -1; errno = saved;
   }
#endif
   if (duplicate >= 0) {
      /* The registry lock protects these immutable access flags while the
       * operation lease keeps the entry alive. */
      struct file_handle_entry *entry = (struct file_handle_entry *)lease;
      if (readable_out != NULL) *readable_out = entry->readable;
      if (writable_out != NULL) *writable_out = entry->writable;
   }
   {
      int saved = errno;
      wfile_end_io(lease);
      if (duplicate < 0) { SetLastError(error_from_errno(saved)); return 0; }
   }
   *fd_out = duplicate;
   return 1;
}

/* Return 1 if closed, 0 if this is not a file handle, -1 on close failure. */
int wfile_close_handle(HANDLE handle)
{
   struct file_handle_entry *entry;
   int fd, rc, saved = 0;
   if (handle == NULL || handle == INVALID_HANDLE_VALUE) return 0;
   pthread_mutex_lock(&g_file_handles_lock);
   entry = find_file_handle_locked(handle);
   if (entry == NULL) { pthread_mutex_unlock(&g_file_handles_lock); return 0; }
   entry->closing = 1;
   TAILQ_REMOVE(&g_file_handles, entry, entries);
   while (entry->active_ops != 0)
      pthread_cond_wait(&g_file_handles_idle, &g_file_handles_lock);
   fd = entry->fd; entry->fd = -1;
   pthread_mutex_unlock(&g_file_handles_lock);
   rc = close(fd);
   if (rc != 0) saved = errno;
   while (entry->locks != NULL) {
      struct file_lock_range *range = entry->locks;
      entry->locks = range->next;
      free(range);
   }
   pthread_mutex_destroy(&entry->io_lock);
   free(entry);
   if (saved != 0) { SetLastError(error_from_errno(saved)); return -1; }
   return 1;
}

static BOOL file_io(HANDLE handle, void *buffer, DWORD count,
                    LPDWORD transferred, LPOVERLAPPED overlapped, int is_write)
{
   int fd, access = is_write ? 2 : 1;
   void *lease = NULL;
   ssize_t n;
   if (transferred == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
   *transferred = 0;
   if (overlapped != NULL) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return FALSE; }
   if (count != 0 && buffer == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
   if (!wfile_begin_io(handle, access, &fd, &lease)) return FALSE;
   if (count == 0) {
      wfile_end_io(lease);
      return TRUE;
   }
   do {
      n = is_write ? write(fd, buffer, (size_t)count) : read(fd, buffer, (size_t)count);
   } while (n < 0 && errno == EINTR);
   {
      int saved = errno;
      wfile_end_io(lease);
      if (n < 0) {
         SetLastError(error_from_errno(saved));
         return FALSE;
      }
   }
   /* For synchronous disk-file I/O, Win32 reports EOF as a successful
    * zero-byte read. ERROR_HANDLE_EOF is reserved for APIs/contexts whose
    * contract explicitly reports it; returning FALSE here breaks the normal
    * ReadFile loop idiom (TRUE + zero bytes means end of stream). */
   *transferred = (DWORD)n;
   return TRUE;
}

BOOL WINAPI ReadFile(HANDLE hFile, LPVOID lpBuffer, DWORD nNumberOfBytesToRead,
                     LPDWORD lpNumberOfBytesRead, LPOVERLAPPED lpOverlapped)
{
   return file_io(hFile, lpBuffer, nNumberOfBytesToRead, lpNumberOfBytesRead,
                  lpOverlapped, 0);
}

BOOL WINAPI WriteFile(HANDLE hFile, LPCVOID lpBuffer, DWORD nNumberOfBytesToWrite,
                      LPDWORD lpNumberOfBytesWritten, LPOVERLAPPED lpOverlapped)
{
   /* The POSIX write API does not modify the supplied buffer. */
   return file_io(hFile, (void *)lpBuffer, nNumberOfBytesToWrite,
                  lpNumberOfBytesWritten, lpOverlapped, 1);
}

BOOL WINAPI FlushFileBuffers(HANDLE hFile)
{
   int fd;
   void *lease = NULL;
   if (!wfile_begin_io(hFile, 2, &fd, &lease)) return FALSE;
   if (fsync(fd) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   wfile_end_io(lease);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}


#ifndef FILE_BEGIN
#define FILE_BEGIN   0u
#define FILE_CURRENT 1u
#define FILE_END     2u
#endif
#ifndef INVALID_SET_FILE_POINTER
#define INVALID_SET_FILE_POINTER 0xFFFFFFFFu
#endif
#ifndef INVALID_FILE_SIZE
#define INVALID_FILE_SIZE 0xFFFFFFFFu
#endif

static int file_seek_origin(DWORD method, int *origin)
{
   switch (method) {
      case FILE_BEGIN: *origin = SEEK_SET; return 1;
      case FILE_CURRENT: *origin = SEEK_CUR; return 1;
      case FILE_END: *origin = SEEK_END; return 1;
      default: SetLastError(ERROR_INVALID_PARAMETER); return 0;
   }
}

BOOL WINAPI SetFilePointerEx(HANDLE hFile, LARGE_INTEGER liDistanceToMove,
                             PLARGE_INTEGER lpNewFilePointer, DWORD dwMoveMethod)
{
   int fd, origin;
   void *lease = NULL;
   off_t result;
   if (!file_seek_origin(dwMoveMethod, &origin)) return FALSE;
   if (!wfile_begin_io(hFile, 0, &fd, &lease)) return FALSE;
   /* Reject values not representable by off_t before lseek can truncate them. */
   if ((int64_t)(off_t)liDistanceToMove.QuadPart != liDistanceToMove.QuadPart) {
      wfile_end_io(lease);
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   result = lseek(fd, (off_t)liDistanceToMove.QuadPart, origin);
   {
      int saved = errno;
      wfile_end_io(lease);
      if (result == (off_t)-1) {
         SetLastError(saved == EINVAL && liDistanceToMove.QuadPart < 0
                      ? ERROR_NEGATIVE_SEEK : error_from_errno(saved));
         return FALSE;
      }
   }
   if (lpNewFilePointer != NULL) lpNewFilePointer->QuadPart = (LONGLONG)result;
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

DWORD WINAPI SetFilePointer(HANDLE hFile, LONG lDistanceToMove,
                            PLONG lpDistanceToMoveHigh, DWORD dwMoveMethod)
{
   LARGE_INTEGER distance, result;
   int64_t combined;
   if (lpDistanceToMoveHigh != NULL)
      combined = (int64_t)(*lpDistanceToMoveHigh) * INT64_C(4294967296) + (uint32_t)lDistanceToMove;
   else
      combined = (int64_t)lDistanceToMove;
   distance.QuadPart = combined;
   if (!SetFilePointerEx(hFile, distance, &result, dwMoveMethod))
      return INVALID_SET_FILE_POINTER;
   if (lpDistanceToMoveHigh != NULL)
      *lpDistanceToMoveHigh = (LONG)(result.QuadPart >> 32);
   return (DWORD)((uint64_t)result.QuadPart & UINT32_MAX);
}

BOOL WINAPI GetFileSizeEx(HANDLE hFile, PLARGE_INTEGER lpFileSize)
{
   int fd;
   void *lease = NULL;
   struct stat st;
   if (lpFileSize == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
   if (!wfile_begin_io(hFile, 0, &fd, &lease)) return FALSE;
   if (fstat(fd, &st) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   wfile_end_io(lease);
   if (st.st_size < 0) { SetLastError(ERROR_INVALID_DATA); return FALSE; }
   lpFileSize->QuadPart = (LONGLONG)st.st_size;
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

/* Set the logical end of file at the handle's current file position.
 * wfile_begin_io serializes the position query and truncate against other
 * operations on this handle, and enforces write access before touching data. */
BOOL WINAPI SetEndOfFile(HANDLE hFile)
{
   int fd;
   void *lease = NULL;
   off_t position;

   if (!wfile_begin_io(hFile, 2, &fd, &lease)) return FALSE;
   position = lseek(fd, (off_t)0, SEEK_CUR);
   if (position == (off_t)-1) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   if (position < 0 || (int64_t)position < 0) {
      wfile_end_io(lease);
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (ftruncate(fd, position) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   wfile_end_io(lease);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

BOOL WINAPI GetFileTime(HANDLE hFile, LPFILETIME lpCreationTime,
                        LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime)
{
   int fd;
   void *lease = NULL;
   struct stat st;
   FILETIME creation_time, access_time, write_time;

   /* Even with all outputs omitted, validate and pin the handle as Win32 does. */
   if (!wfile_begin_io(hFile, 0, &fd, &lease)) return FALSE;
   if (fstat(fd, &st) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   wfile_end_io(lease);

   /* POSIX has no portable birth-time field. st_ctim is an explicit best-effort
    * proxy (metadata-change time), not a claim that inode creation is known. */
   if ((lpCreationTime != NULL && !winapi_timespec_to_filetime(&st.st_ctim, &creation_time)) ||
       (lpLastAccessTime != NULL && !winapi_timespec_to_filetime(&st.st_atim, &access_time)) ||
       (lpLastWriteTime != NULL && !winapi_timespec_to_filetime(&st.st_mtim, &write_time))) {
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
   }
   if (lpCreationTime != NULL) *lpCreationTime = creation_time;
   if (lpLastAccessTime != NULL) *lpLastAccessTime = access_time;
   if (lpLastWriteTime != NULL) *lpLastWriteTime = write_time;
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}


/* Convert a Win32 FILETIME (100 ns ticks since 1601-01-01 UTC) into a POSIX
 * timespec. Values before the Unix epoch and values not representable by the
 * host time_t are rejected rather than wrapped. */
static int filetime_to_timespec_checked(const FILETIME *ft, struct timespec *ts)
{
   const uint64_t ticks_per_second = UINT64_C(10000000);
   const int64_t epoch_diff = INT64_C(11644473600);
   uint64_t ticks, seconds_since_1601, remainder;
   int64_t unix_seconds;
   time_t converted;
   if (ft == NULL || ts == NULL) return 0;
   ticks = ((uint64_t)ft->dwHighDateTime << 32) | ft->dwLowDateTime;
   seconds_since_1601 = ticks / ticks_per_second;
   remainder = ticks % ticks_per_second;
   if (seconds_since_1601 > (uint64_t)INT64_MAX) return 0;
   if ((int64_t)seconds_since_1601 < epoch_diff) {
      /* POSIX timestamps before 1970 can be representable, so keep the
       * subtraction signed after checking that it cannot underflow. */
      unix_seconds = (int64_t)seconds_since_1601 - epoch_diff;
   } else {
      unix_seconds = (int64_t)seconds_since_1601 - epoch_diff;
   }
   converted = (time_t)unix_seconds;
   if ((int64_t)converted != unix_seconds) return 0;
   ts->tv_sec = converted;
   ts->tv_nsec = (long)(remainder * UINT64_C(100));
   return 1;
}

BOOL WINAPI SetFileTime(HANDLE hFile, const FILETIME *lpCreationTime,
                        const FILETIME *lpLastAccessTime,
                        const FILETIME *lpLastWriteTime)
{
   int fd;
   void *lease = NULL;
   struct timespec times[2];
   int have_access = lpLastAccessTime != NULL;
   int have_write = lpLastWriteTime != NULL;

   /* Pin and validate the handle/access before reporting backend limitations. */
   if (!wfile_begin_io(hFile, 2, &fd, &lease)) return FALSE;

   /* A portable POSIX backend cannot set an inode birth/creation timestamp.
    * Reject that request before changing either supported timestamp. */
   if (lpCreationTime != NULL) {
      wfile_end_io(lease);
      SetLastError(ERROR_NOT_SUPPORTED);
      return FALSE;
   }
   if (!have_access && !have_write) {
      /* Win32 accepts both timestamps omitted as a no-op, but the handle must
       * still be live and writable for this backend's attribute operation. */
      wfile_end_io(lease);
      SetLastError(ERROR_SUCCESS);
      return TRUE;
   }
   if ((have_access && !filetime_to_timespec_checked(lpLastAccessTime, &times[0])) ||
       (have_write && !filetime_to_timespec_checked(lpLastWriteTime, &times[1]))) {
      wfile_end_io(lease);
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   /* futimens requires both entries: UTIME_OMIT preserves a timestamp the
    * caller did not request. */
   if (!have_access) { times[0].tv_sec = 0; times[0].tv_nsec = UTIME_OMIT; }
   if (!have_write) { times[1].tv_sec = 0; times[1].tv_nsec = UTIME_OMIT; }
   if (futimens(fd, times) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   wfile_end_io(lease);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

static int file_lock_range_to_off_t(uint64_t value, off_t *out)
{
   off_t converted;
   if (out == NULL || value > (uint64_t)INT64_MAX) return 0;
   converted = (off_t)value;
   if (converted < 0 || (uint64_t)converted != value) return 0;
   *out = converted;
   return 1;
}

static BOOL winapi_file_lock(HANDLE hFile, DWORD offset_low, DWORD offset_high,
                             DWORD length_low, DWORD length_high, int unlock)
{
   uint64_t offset = ((uint64_t)offset_high << 32) | offset_low;
   uint64_t length = ((uint64_t)length_high << 32) | length_low;
   struct flock lock;
   struct file_handle_entry *entry;
   struct file_lock_range **link, *range;
   int fd, command, saved;
   off_t start, lock_length;
   void *lease = NULL;

   if (length == 0 || offset > (uint64_t)INT64_MAX ||
       length > (uint64_t)INT64_MAX || offset > (uint64_t)INT64_MAX - length ||
       !file_lock_range_to_off_t(offset, &start) ||
       !file_lock_range_to_off_t(length, &lock_length)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (!wfile_begin_io(hFile, 0, &fd, &lease)) return FALSE;
   entry = (struct file_handle_entry *)lease;
   link = &entry->locks;
   while (*link != NULL && ((*link)->offset != offset || (*link)->length != length)) {
      uint64_t existing_end = (*link)->offset + (*link)->length;
      uint64_t requested_end = offset + length;
      if (!unlock && offset < existing_end && (*link)->offset < requested_end) {
         wfile_end_io(lease);
         SetLastError(ERROR_LOCK_VIOLATION);
         return FALSE;
      }
      link = &(*link)->next;
   }

   if (unlock && *link == NULL) {
      wfile_end_io(lease);
      SetLastError(ERROR_NOT_LOCKED);
      return FALSE;
   }
   if (!unlock && *link != NULL) {
      /* The same handle already owns this exact range. Treat the repeated
       * request as success, avoiding duplicate bookkeeping on OFD-lock hosts. */
      wfile_end_io(lease);
      SetLastError(ERROR_SUCCESS);
      return TRUE;
   }

   memset(&lock, 0, sizeof(lock));
   lock.l_type = unlock ? F_UNLCK : F_WRLCK;
   lock.l_whence = SEEK_SET;
   lock.l_start = start;
   lock.l_len = lock_length;
#ifdef F_OFD_SETLK
   command = F_OFD_SETLK;
#else
   command = F_SETLK;
#endif
   if (fcntl(fd, command, &lock) != 0) {
      saved = errno;
      wfile_end_io(lease);
      if (saved == EACCES || saved == EAGAIN) SetLastError(ERROR_LOCK_VIOLATION);
      else SetLastError(error_from_errno(saved));
      return FALSE;
   }
   if (unlock) {
      range = *link;
      *link = range->next;
      free(range);
   } else {
      range = (struct file_lock_range *)malloc(sizeof(*range));
      if (range == NULL) {
         /* Undo the OS lock if bookkeeping cannot be allocated. */
         lock.l_type = F_UNLCK;
         (void)fcntl(fd, command, &lock);
         wfile_end_io(lease);
         SetLastError(ERROR_NOT_ENOUGH_MEMORY);
         return FALSE;
      }
      range->offset = offset;
      range->length = length;
      range->next = entry->locks;
      entry->locks = range;
   }
   wfile_end_io(lease);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

BOOL WINAPI LockFile(HANDLE hFile, DWORD dwFileOffsetLow, DWORD dwFileOffsetHigh,
                     DWORD nNumberOfBytesToLockLow, DWORD nNumberOfBytesToLockHigh)
{
   return winapi_file_lock(hFile, dwFileOffsetLow, dwFileOffsetHigh,
                           nNumberOfBytesToLockLow, nNumberOfBytesToLockHigh, 0);
}

BOOL WINAPI UnlockFile(HANDLE hFile, DWORD dwFileOffsetLow, DWORD dwFileOffsetHigh,
                       DWORD nNumberOfBytesToUnlockLow, DWORD nNumberOfBytesToUnlockHigh)
{
   return winapi_file_lock(hFile, dwFileOffsetLow, dwFileOffsetHigh,
                           nNumberOfBytesToUnlockLow, nNumberOfBytesToUnlockHigh, 1);
}

DWORD WINAPI GetFileSize(HANDLE hFile, LPDWORD lpFileSizeHigh)
{
   LARGE_INTEGER size;
   if (!GetFileSizeEx(hFile, &size)) return INVALID_FILE_SIZE;
   if (lpFileSizeHigh != NULL) *lpFileSizeHigh = (DWORD)((uint64_t)size.QuadPart >> 32);
   return (DWORD)((uint64_t)size.QuadPart & UINT32_MAX);
}

/* This backend registers only ordinary disk files in the file-handle table.
 * Other Win32 object types are handled by their own registries and therefore
 * deliberately return FILE_TYPE_UNKNOWN here, as an invalid file handle. */
DWORD WINAPI GetFileType(HANDLE hFile)
{
   int fd;
   void *lease = NULL;
   struct stat st;

   if (!wfile_begin_io(hFile, 0, &fd, &lease))
      return FILE_TYPE_UNKNOWN;
   if (fstat(fd, &st) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FILE_TYPE_UNKNOWN;
   }
   wfile_end_io(lease);
   if (!S_ISREG(st.st_mode)) {
      SetLastError(ERROR_INVALID_HANDLE);
      return FILE_TYPE_UNKNOWN;
   }
   SetLastError(ERROR_SUCCESS);
   return FILE_TYPE_DISK;
}

/* Return a consistent metadata snapshot while the handle is leased. POSIX
 * does not portably expose birth time, so ctime is used as an explicit proxy,
 * matching GetFileTime's documented limitation in this compatibility layer. */
BOOL WINAPI GetFileInformationByHandle(HANDLE hFile,
                                       LPBY_HANDLE_FILE_INFORMATION info)
{
   int fd;
   void *lease = NULL;
   struct stat st;
   BY_HANDLE_FILE_INFORMATION result;
   uint64_t size, inode;

   if (info == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (!wfile_begin_io(hFile, 0, &fd, &lease)) return FALSE;
   if (fstat(fd, &st) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   wfile_end_io(lease);
   if (!S_ISREG(st.st_mode) || st.st_size < 0) {
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
   }
   memset(&result, 0, sizeof(result));
   result.dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
   if ((st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
      result.dwFileAttributes |= FILE_ATTRIBUTE_READONLY;
   if (!winapi_timespec_to_filetime(&st.st_ctim, &result.ftCreationTime) ||
       !winapi_timespec_to_filetime(&st.st_atim, &result.ftLastAccessTime) ||
       !winapi_timespec_to_filetime(&st.st_mtim, &result.ftLastWriteTime)) {
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
   }
   size = (uint64_t)st.st_size;
   inode = (uint64_t)st.st_ino;
   result.dwVolumeSerialNumber = (DWORD)((uint64_t)st.st_dev & UINT32_MAX);
   result.nFileSizeHigh = (DWORD)(size >> 32);
   result.nFileSizeLow = (DWORD)(size & UINT32_MAX);
   result.nNumberOfLinks = (DWORD)((uint64_t)st.st_nlink > UINT32_MAX ?
                                   UINT32_MAX : (uint64_t)st.st_nlink);
   result.nFileIndexHigh = (DWORD)(inode >> 32);
   result.nFileIndexLow = (DWORD)(inode & UINT32_MAX);
   *info = result;
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

/* Convert a UTF-8 path into the 16-bit UTF-16 representation required by
 * FILE_NAME_INFO. Reject malformed UTF-8 rather than silently changing a name. */
static int utf8_path_to_utf16(const char *src, WORD **out_units, size_t *out_count)
{
   size_t bytes, i = 0, count = 0, capacity;
   WORD *units;
   if (src == NULL || out_units == NULL || out_count == NULL) {
      errno = EINVAL;
      return -1;
   }
   bytes = strlen(src);
   if (bytes > (SIZE_MAX / sizeof(WORD)) - 1) { errno = EOVERFLOW; return -1; }
   capacity = bytes + 1; /* UTF-16 units never outnumber UTF-8 bytes. */
   units = (WORD *)malloc(capacity * sizeof(*units));
   if (units == NULL) { errno = ENOMEM; return -1; }
   while (i < bytes) {
      uint32_t cp;
      unsigned char c = (unsigned char)src[i++];
      unsigned int extra;
      if (c < 0x80) { cp = c; extra = 0; }
      else if (c >= 0xC2 && c <= 0xDF) { cp = c & 0x1Fu; extra = 1; }
      else if (c >= 0xE0 && c <= 0xEF) { cp = c & 0x0Fu; extra = 2; }
      else if (c >= 0xF0 && c <= 0xF4) { cp = c & 0x07u; extra = 3; }
      else { free(units); errno = EILSEQ; return -1; }
      if (i + extra > bytes) { free(units); errno = EILSEQ; return -1; }
      for (unsigned int j = 0; j < extra; ++j) {
         unsigned char tail = (unsigned char)src[i++];
         if ((tail & 0xC0u) != 0x80u) { free(units); errno = EILSEQ; return -1; }
         cp = (cp << 6) | (uint32_t)(tail & 0x3Fu);
      }
      if ((extra == 1 && cp < 0x80) || (extra == 2 && cp < 0x800) ||
          (extra == 3 && cp < 0x10000) || cp > 0x10FFFF ||
          (cp >= 0xD800 && cp <= 0xDFFF)) {
         free(units); errno = EILSEQ; return -1;
      }
      if (cp <= 0xFFFF) units[count++] = (WORD)cp;
      else {
         cp -= 0x10000;
         units[count++] = (WORD)(0xD800u + (cp >> 10));
         units[count++] = (WORD)(0xDC00u + (cp & 0x3FFu));
      }
   }
   *out_units = units;
   *out_count = count;
   return 0;
}

/* Obtain the descriptor's current kernel-visible name while its operation
 * lease is held. /proc/self/fd resolves relative opens and symlinks to the
 * object actually referenced by the handle. Deleted names are not returned as
 * if they were valid paths. */
static int file_handle_name_utf16(int fd, WORD **units_out, size_t *count_out)
{
#if defined(__linux__)
   char link_name[64];
   char *path = NULL;
   struct stat fd_stat;
   size_t capacity = 256;
   ssize_t length;
   int n = snprintf(link_name, sizeof(link_name), "/proc/self/fd/%d", fd);
   if (n < 0 || (size_t)n >= sizeof(link_name)) { errno = EINVAL; return -1; }
   if (fstat(fd, &fd_stat) != 0) return -1;
   if (fd_stat.st_nlink == 0) { errno = ENOENT; return -1; }
   for (;;) {
      char *grown;
      if (capacity > 1024u * 1024u) { free(path); errno = ENAMETOOLONG; return -1; }
      grown = (char *)realloc(path, capacity + 1);
      if (grown == NULL) { free(path); errno = ENOMEM; return -1; }
      path = grown;
      length = readlink(link_name, path, capacity);
      if (length < 0) { int saved = errno; free(path); errno = saved; return -1; }
      if ((size_t)length < capacity) break;
      capacity *= 2;
   }
   path[length] = '\0';
   if (utf8_path_to_utf16(path, units_out, count_out) != 0) {
      int saved = errno; free(path); errno = saved; return -1;
   }
   free(path);
   return 0;
#else
   (void)fd; (void)units_out; (void)count_out;
   errno = ENOTSUP;
   return -1;
#endif
}

/* Query the information classes whose semantics can be represented without
 * inventing Windows-only metadata. Build into local temporaries so an invalid
 * handle, short buffer or unsupported class never partially changes output. */
BOOL WINAPI GetFileInformationByHandleEx(HANDLE hFile,
                                         FILE_INFO_BY_HANDLE_CLASS info_class,
                                         LPVOID lpFileInformation,
                                         DWORD dwBufferSize)
{
   int fd;
   void *lease = NULL;
   struct stat st;
   DWORD attributes = FILE_ATTRIBUTE_ARCHIVE;
   size_t required;
   WORD *name_units = NULL;
   size_t name_unit_count = 0;
   union {
      FILE_BASIC_INFO basic;
      FILE_STANDARD_INFO standard;
      FILE_ATTRIBUTE_TAG_INFO attribute_tag;
      FILE_ID_INFO id;
      FILE_ALIGNMENT_INFO alignment;
      FILE_NAME_INFO name;
   } result;

   switch (info_class) {
      case FileBasicInfo: required = sizeof(FILE_BASIC_INFO); break;
      case FileStandardInfo: required = sizeof(FILE_STANDARD_INFO); break;
      case FileAttributeTagInfo: required = sizeof(FILE_ATTRIBUTE_TAG_INFO); break;
      case FileIdInfo: required = sizeof(FILE_ID_INFO); break;
      case FileAlignmentInfo: required = sizeof(FILE_ALIGNMENT_INFO); break;
      case FileNameInfo:
      case FileNormalizedNameInfo: required = offsetof(FILE_NAME_INFO, FileName); break;
      default:
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
   }
   if (lpFileInformation == NULL || (size_t)dwBufferSize < required) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (!wfile_begin_io(hFile, 0, &fd, &lease)) return FALSE;
   if (fstat(fd, &st) != 0) {
      int saved = errno;
      wfile_end_io(lease);
      SetLastError(error_from_errno(saved));
      return FALSE;
   }
   if (info_class == FileNameInfo || info_class == FileNormalizedNameInfo) {
      if (file_handle_name_utf16(fd, &name_units, &name_unit_count) != 0) {
         int saved = errno;
         wfile_end_io(lease);
         if (saved == EILSEQ) SetLastError(ERROR_NO_UNICODE_TRANSLATION);
         else SetLastError(error_from_errno(saved));
         return FALSE;
      }
   }
   wfile_end_io(lease);
   if (!S_ISREG(st.st_mode) || st.st_size < 0 || st.st_blocks < 0) {
      free(name_units);
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
   }
   if (name_units != NULL &&
       (name_unit_count > (UINT32_MAX / sizeof(WORD)) ||
        name_unit_count > (SIZE_MAX - offsetof(FILE_NAME_INFO, FileName)) / sizeof(WORD))) {
      free(name_units);
      SetLastError(ERROR_ARITHMETIC_OVERFLOW);
      return FALSE;
   }
   if (name_units != NULL && dwBufferSize <
       offsetof(FILE_NAME_INFO, FileName) + name_unit_count * sizeof(WORD)) {
      free(name_units);
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return FALSE;
   }
   if ((st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
      attributes |= FILE_ATTRIBUTE_READONLY;

   memset(&result, 0, sizeof(result));
   switch (info_class) {
      case FileBasicInfo: {
         FILETIME creation, access, write, change;
         if (!winapi_timespec_to_filetime(&st.st_ctim, &creation) ||
             !winapi_timespec_to_filetime(&st.st_atim, &access) ||
             !winapi_timespec_to_filetime(&st.st_mtim, &write) ||
             !winapi_timespec_to_filetime(&st.st_ctim, &change)) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
         }
         result.basic.CreationTime.QuadPart =
            (LONGLONG)(((uint64_t)creation.dwHighDateTime << 32) | creation.dwLowDateTime);
         result.basic.LastAccessTime.QuadPart =
            (LONGLONG)(((uint64_t)access.dwHighDateTime << 32) | access.dwLowDateTime);
         result.basic.LastWriteTime.QuadPart =
            (LONGLONG)(((uint64_t)write.dwHighDateTime << 32) | write.dwLowDateTime);
         result.basic.ChangeTime.QuadPart =
            (LONGLONG)(((uint64_t)change.dwHighDateTime << 32) | change.dwLowDateTime);
         result.basic.FileAttributes = attributes;
         memcpy(lpFileInformation, &result.basic, sizeof(result.basic));
         break;
      }
      case FileStandardInfo: {
         uint64_t allocation;
         if ((uint64_t)st.st_blocks > UINT64_MAX / UINT64_C(512)) {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
         }
         allocation = (uint64_t)st.st_blocks * UINT64_C(512);
         if (allocation > (uint64_t)INT64_MAX) {
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return FALSE;
         }
         result.standard.AllocationSize.QuadPart = (LONGLONG)allocation;
         result.standard.EndOfFile.QuadPart = (LONGLONG)st.st_size;
         result.standard.NumberOfLinks = (DWORD)((uint64_t)st.st_nlink > UINT32_MAX
                                                   ? UINT32_MAX : (uint64_t)st.st_nlink);
         result.standard.DeletePending = FALSE;
         result.standard.Directory = FALSE;
         memcpy(lpFileInformation, &result.standard, sizeof(result.standard));
         break;
      }
      case FileAttributeTagInfo:
         result.attribute_tag.FileAttributes = attributes;
         result.attribute_tag.ReparseTag = 0;
         memcpy(lpFileInformation, &result.attribute_tag, sizeof(result.attribute_tag));
         break;
      case FileIdInfo: {
         uint64_t inode = (uint64_t)st.st_ino;
         result.id.VolumeSerialNumber = (ULONGLONG)(uint64_t)st.st_dev;
         /* POSIX exposes a device identifier and an inode number, not a
          * Windows 128-bit file identifier. Preserve the inode in the low
          * 64 bits and zero-extend it; consumers must treat the ID as opaque. */
         memset(result.id.FileId.Identifier, 0, sizeof(result.id.FileId.Identifier));
         for (unsigned int i = 0; i < 8; ++i)
            result.id.FileId.Identifier[i] = (BYTE)(inode >> (i * 8));
         memcpy(lpFileInformation, &result.id, sizeof(result.id));
         break;
      }
      case FileAlignmentInfo:
         /* POSIX regular-file descriptors permit byte-granular I/O; no
          * alignment restriction is imposed by this backend. */
         result.alignment.AlignmentRequirement = 0;
         memcpy(lpFileInformation, &result.alignment, sizeof(result.alignment));
         break;
      case FileNameInfo:
      case FileNormalizedNameInfo: {
         FILE_NAME_INFO *name = (FILE_NAME_INFO *)lpFileInformation;
         size_t name_bytes = name_unit_count * sizeof(WORD);
         name->FileNameLength = (DWORD)name_bytes;
         if (name_bytes != 0)
            memcpy(name->FileName, name_units, name_bytes);
         free(name_units);
         name_units = NULL;
         break;
      }
      default:
         /* The class was validated above; keep defensive behavior if extended. */
         free(name_units);
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
   }
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}


/* Apply the two information classes for which this backend can preserve the
 * Win32 contract without fabricating metadata. Unsupported fields are checked
 * before any mutation, so failure never leaves a half-applied request. */
BOOL WINAPI SetFileInformationByHandle(HANDLE hFile,
                                       FILE_INFO_BY_HANDLE_CLASS info_class,
                                       LPVOID information,
                                       DWORD buffer_size)
{
   int fd;
   void *lease = NULL;
   if (information == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }

   if (info_class == FileEndOfFileInfo) {
      FILE_END_OF_FILE_INFO requested;
      off_t end;
      if ((size_t)buffer_size < sizeof(requested)) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
      }
      memcpy(&requested, information, sizeof(requested));
      if (requested.EndOfFile.QuadPart < 0) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
      }
      end = (off_t)requested.EndOfFile.QuadPart;
      if (end < 0 || (LONGLONG)end != requested.EndOfFile.QuadPart) {
         SetLastError(ERROR_ARITHMETIC_OVERFLOW);
         return FALSE;
      }
      if (!wfile_begin_io(hFile, 2, &fd, &lease)) return FALSE;
      if (ftruncate(fd, end) != 0) {
         int saved = errno;
         wfile_end_io(lease);
         SetLastError(error_from_errno(saved));
         return FALSE;
      }
      wfile_end_io(lease);
      SetLastError(ERROR_SUCCESS);
      return TRUE;
   }

   if (info_class == FileBasicInfo) {
      FILE_BASIC_INFO requested;
      struct timespec times[2];
      int have_access, have_write;
      if ((size_t)buffer_size < sizeof(requested)) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
      }
      memcpy(&requested, information, sizeof(requested));
      /* A zero FILETIME means "leave unchanged". Birth time, change time,
       * and Windows attribute bits cannot be faithfully set by this backend.
       * Reject them before touching either supported timestamp. */
      if (requested.CreationTime.QuadPart != 0 ||
          requested.ChangeTime.QuadPart != 0 || requested.FileAttributes != 0) {
         SetLastError(ERROR_NOT_SUPPORTED);
         return FALSE;
      }
      have_access = requested.LastAccessTime.QuadPart != 0;
      have_write = requested.LastWriteTime.QuadPart != 0;
      if (have_access) {
         FILETIME value;
         value.dwLowDateTime = (DWORD)(uint64_t)requested.LastAccessTime.QuadPart;
         value.dwHighDateTime = (DWORD)((uint64_t)requested.LastAccessTime.QuadPart >> 32);
         if (!filetime_to_timespec_checked(&value, &times[0])) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
         }
      }
      if (have_write) {
         FILETIME value;
         value.dwLowDateTime = (DWORD)(uint64_t)requested.LastWriteTime.QuadPart;
         value.dwHighDateTime = (DWORD)((uint64_t)requested.LastWriteTime.QuadPart >> 32);
         if (!filetime_to_timespec_checked(&value, &times[1])) {
            SetLastError(ERROR_INVALID_PARAMETER);
            return FALSE;
         }
      }
      if (!wfile_begin_io(hFile, 2, &fd, &lease)) return FALSE;
      if (!have_access && !have_write) {
         wfile_end_io(lease);
         SetLastError(ERROR_SUCCESS);
         return TRUE;
      }
      if (!have_access) { times[0].tv_sec = 0; times[0].tv_nsec = UTIME_OMIT; }
      if (!have_write) { times[1].tv_sec = 0; times[1].tv_nsec = UTIME_OMIT; }
      if (futimens(fd, times) != 0) {
         int saved = errno;
         wfile_end_io(lease);
         SetLastError(error_from_errno(saved));
         return FALSE;
      }
      wfile_end_io(lease);
      SetLastError(ERROR_SUCCESS);
      return TRUE;
   }

   SetLastError(ERROR_INVALID_PARAMETER);
   return FALSE;
}

#if !defined(ENABLE_WTHREADS) || !ENABLE_WTHREADS
BOOL WINAPI CloseHandle(HANDLE handle)
{
   int file_result = wfile_close_handle(handle);
   if (file_result > 0) return TRUE;
   if (file_result < 0) return FALSE;
#if defined(ENABLE_WSHMEM) && ENABLE_WSHMEM
   {
      int shmem_result = wshmem_close_handle(handle);
      if (shmem_result > 0) return TRUE;
      if (shmem_result < 0) return FALSE;
   }
#endif
   SetLastError(ERROR_INVALID_HANDLE);
   return FALSE;
}
#endif
