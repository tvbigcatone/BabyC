#include "windows.h"

/* Use the POSIX mkdir(2) for private runtime coordination directories. */
#ifdef mkdir
#undef mkdir
#endif
#include "wprivate.h"

#if (ENABLE_WSHMEM == 1)

#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/file.h>
#include <unistd.h>

extern struct _fileViewsHead *fileviews_head;
extern struct _mapFilesHead *mapfiles_head;

#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
/* One-shot syscall failures used only by the regression suite. Tests invoke
 * these hooks serially, before entering the API under test. */
static int g_test_fail_pread;
static int g_test_fail_fchmod;
static int g_test_fail_shm_open_create;
static int g_test_fail_flock_ex;
static int g_test_fail_flock_un;
static int g_test_fail_shm_unlink;
static long g_test_calloc_successes_before_failure = -1;
static long g_test_fstat_successes_before_failure = -1;
void winapi_test_shmem_fail_next_pread(void) { g_test_fail_pread = 1; }
void winapi_test_shmem_fail_next_fchmod(void) { g_test_fail_fchmod = 1; }
void winapi_test_shmem_fail_next_shm_open_create(void) { g_test_fail_shm_open_create = 1; }
void winapi_test_shmem_fail_next_flock_ex(void) { g_test_fail_flock_ex = 1; }
void winapi_test_shmem_fail_next_flock_un(void) { g_test_fail_flock_un = 1; }
void winapi_test_shmem_fail_next_shm_unlink(void) { g_test_fail_shm_unlink = 1; }
void winapi_test_shmem_fail_next_calloc(void) { g_test_calloc_successes_before_failure = 0; }
void winapi_test_shmem_fail_calloc_after(long successful_calls)
{
   g_test_calloc_successes_before_failure = successful_calls < 0 ? -1 : successful_calls;
}
void winapi_test_shmem_fail_fstat_after(long successful_calls)
{
   g_test_fstat_successes_before_failure = successful_calls < 0 ? -1 : successful_calls;
}
#endif

static int shmem_fstat(int fd, struct stat *st)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_fstat_successes_before_failure == 0) {
      g_test_fstat_successes_before_failure = -1;
      errno = EIO;
      return -1;
   }
   if (g_test_fstat_successes_before_failure > 0)
      --g_test_fstat_successes_before_failure;
#endif
   return fstat(fd, st);
}

static void *shmem_calloc(size_t count, size_t size)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_calloc_successes_before_failure == 0) {
      g_test_calloc_successes_before_failure = -1;
      errno = ENOMEM;
      return NULL;
   }
   if (g_test_calloc_successes_before_failure > 0)
      --g_test_calloc_successes_before_failure;
#endif
   return calloc(count, size);
}

static ssize_t shmem_pread(int fd, void *buffer, size_t size, off_t offset)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_fail_pread) { g_test_fail_pread = 0; errno = EIO; return -1; }
#endif
   return pread(fd, buffer, size, offset);
}

static int shmem_fchmod(int fd, mode_t mode)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_fail_fchmod) { g_test_fail_fchmod = 0; errno = EIO; return -1; }
#endif
   return fchmod(fd, mode);
}

static int shmem_shm_open(const char *name, int oflag, mode_t mode)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_fail_shm_open_create && (oflag & O_CREAT) && (oflag & O_EXCL)) {
      g_test_fail_shm_open_create = 0; errno = EIO; return -1;
   }
#endif
   return shm_open(name, oflag, mode);
}

static int shmem_flock(int fd, int operation)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_fail_flock_ex && (operation & LOCK_EX)) {
      g_test_fail_flock_ex = 0; errno = EIO; return -1;
   }
   if (g_test_fail_flock_un && (operation & LOCK_UN)) {
      g_test_fail_flock_un = 0; errno = EIO; return -1;
   }
#endif
   return flock(fd, operation);
}

static int shmem_shm_unlink(const char *name)
{
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
   if (g_test_fail_shm_unlink) {
      g_test_fail_shm_unlink = 0; errno = EIO; return -1;
   }
#endif
   return shm_unlink(name);
}

/* The mapping registries are process-global and are not protected by the
 * thread wait lock. Keep their mutations and traversals under a private lock. */
static pthread_mutex_t g_shmem_lock = PTHREAD_MUTEX_INITIALIZER;

/* POSIX shared-memory cleanup is idempotent: an already-removed name is
 * success, and an interrupted unlink is retried before reporting failure. */
static int unlink_posix_name(const char *name)
{
   int result;
   do {
      result = shmem_shm_unlink(name);
   } while (result != 0 && errno == EINTR);
   return result == 0 || errno == ENOENT;
}

/* The lock file also carries a tiny recovery record. Its state byte is set
 * to INITIALIZING before a new POSIX object is published and changed to READY
 * only after sizing/protection are complete. If a process dies, flock releases
 * automatically; the next cooperating caller removes the stale object while
 * holding the same lock, then proceeds normally. */
#define SHM_LOCK_RECORD_MAGIC UINT64_C(0x57494e53484d3031)
#define SHM_LOCK_STATE_OFFSET (sizeof(uint64_t) + MAX_PATH + 1)
#define SHM_LOCK_READY 0u
#define SHM_LOCK_INITIALIZING 1u

typedef struct shm_lock_record {
   uint64_t magic;
   char name[MAX_PATH + 1];
   unsigned char state;
} shm_lock_record;

static int pread_full(int fd, void *buffer, size_t size, off_t offset)
{
   unsigned char *p = (unsigned char *)buffer;
   size_t done = 0;
   while (done < size) {
      ssize_t n = shmem_pread(fd, p + done, size - done, offset + (off_t)done);
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) return 0;
      done += (size_t)n;
   }
   return 1;
}

#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
/* Test-only, one-shot short-write injection. This symbol is absent from
 * ordinary production builds and is not part of the public WinAPI surface. */
static ssize_t g_test_pwrite_budget = -1;
static int g_test_fail_rollback_ftruncate;
void winapi_test_shmem_fail_pwrite_after(ssize_t bytes)
{
   g_test_pwrite_budget = bytes;
}
void winapi_test_shmem_fail_rollback_ftruncate(void)
{
   g_test_fail_rollback_ftruncate = 1;
}
static int rollback_lock_record(int fd)
{
   if (g_test_fail_rollback_ftruncate) {
      g_test_fail_rollback_ftruncate = 0;
      errno = EIO;
      return -1;
   }
   return ftruncate(fd, 0);
}
#else
static int rollback_lock_record(int fd)
{
   return ftruncate(fd, 0);
}
#endif

static int pwrite_full(int fd, const void *buffer, size_t size, off_t offset)
{
   const unsigned char *p = (const unsigned char *)buffer;
   size_t done = 0;
   while (done < size) {
      ssize_t n;
#if defined(WINAPI_TEST_FAULT_INJECTION) && WINAPI_TEST_FAULT_INJECTION
      if (g_test_pwrite_budget == 0) {
         g_test_pwrite_budget = -1;
         errno = EIO;
         return 0;
      }
      if (g_test_pwrite_budget > 0 && (size_t)g_test_pwrite_budget < size - done) {
         n = pwrite(fd, p + done, (size_t)g_test_pwrite_budget, offset + (off_t)done);
         if (n > 0) g_test_pwrite_budget -= n;
      } else {
         n = pwrite(fd, p + done, size - done, offset + (off_t)done);
         if (n > 0 && g_test_pwrite_budget > 0) g_test_pwrite_budget -= n;
      }
#else
      n = pwrite(fd, p + done, size - done, offset + (off_t)done);
#endif
      if (n < 0 && errno == EINTR) continue;
      if (n <= 0) { if (n == 0) errno = EIO; return 0; }
      done += (size_t)n;
   }
   return 1;
}

static int lock_record_matches(const shm_lock_record *record, const char *name)
{
   return record->magic == SHM_LOCK_RECORD_MAGIC &&
      memchr(record->name, '\0', sizeof(record->name)) != NULL &&
      strncmp(record->name, name, sizeof(record->name)) == 0 &&
      (record->state == SHM_LOCK_READY || record->state == SHM_LOCK_INITIALIZING);
}

static int read_lock_record(int fd, const char *name, shm_lock_record *record)
{
   return pread_full(fd, record, sizeof(*record), 0) && lock_record_matches(record, name);
}

static int write_lock_state(int fd, const char *name, unsigned char state)
{
   shm_lock_record record;
   struct stat st;
   if (state != SHM_LOCK_READY && state != SHM_LOCK_INITIALIZING) { errno = EINVAL; return 0; }
   if (shmem_fstat(fd, &st) != 0) return 0;
   if (st.st_size == 0) {
      memset(&record, 0, sizeof(record));
      record.magic = SHM_LOCK_RECORD_MAGIC;
      snprintf(record.name, sizeof(record.name), "%s", name);
      record.state = state;
      /* pwrite extends an empty file, so pre-sizing it only creates a crash
       * window in which a full-sized but unwritten record looks corrupt. If a
       * write fails after a partial transfer, roll the file back to empty so
       * the next caller can initialize it again. A process crash during the
       * write can still leave a corrupt record; readers deliberately fail
       * closed rather than guessing whether that record is trustworthy. */
      if (!pwrite_full(fd, &record, sizeof(record), 0)) {
         int saved = errno ? errno : EIO;
         (void)rollback_lock_record(fd);
         errno = saved;
         return 0;
      }
      return 1;
   }
   if (st.st_size != (off_t)sizeof(record) || !read_lock_record(fd, name, &record)) {
      errno = EIO;
      return 0;
   }
   return pwrite_full(fd, &state, sizeof(state), (off_t)SHM_LOCK_STATE_OFFSET);
}

static void hash_mapping_name(const char *name, uint64_t *hash_a, uint64_t *hash_b)
{
   const unsigned char *p = (const unsigned char *)name;
   uint64_t a = UINT64_C(1469598103934665603); /* FNV-1a 64-bit */
   uint64_t b = UINT64_C(0x9e3779b97f4a7c15); /* independent rolling hash */
   while (*p != 0) {
      a ^= *p;
      a *= UINT64_C(1099511628211);
      b ^= (uint64_t)*p + UINT64_C(0x9e3779b97f4a7c15) + (b << 6) + (b >> 2);
      ++p;
   }
   *hash_a = a;
   *hash_b = b;
}

static int lock_named_mapping(const char *name, int *lock_fd)
{
   uint64_t hash_a, hash_b;
   char directory[96];
   char filename[96];
   char posix_name[MAX_PATH + 2];
   struct stat st;
   shm_lock_record record;
   int dirfd, fd, saved;
   snprintf(directory, sizeof(directory), "/tmp/winapi-shm-locks-%lu", (unsigned long)getuid());
   if (mkdir(directory, S_IRWXU) != 0 && errno != EEXIST) return 0;
   dirfd = open(directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
   if (dirfd < 0) return 0;
   if (shmem_fstat(dirfd, &st) != 0) {
      saved = errno; (void)close(dirfd); errno = saved; return 0;
   }
   if (!S_ISDIR(st.st_mode) || st.st_uid != getuid() ||
       (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
      (void)close(dirfd); errno = EACCES; return 0;
   }
   hash_mapping_name(name, &hash_a, &hash_b);
   snprintf(filename, sizeof(filename), "lock-%016llx-%016llx",
      (unsigned long long)hash_a, (unsigned long long)hash_b);
   fd = openat(dirfd, filename, O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW,
      S_IRUSR | S_IWUSR);
   saved = errno;
   close(dirfd);
   if (fd < 0) { errno = saved; return 0; }
   if (shmem_fstat(fd, &st) != 0) {
      saved = errno; (void)close(fd); errno = saved; return 0;
   }
   if (!S_ISREG(st.st_mode) || st.st_uid != getuid() ||
       st.st_nlink != 1 || (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
      (void)close(fd); errno = EACCES; return 0;
   }
   while (shmem_flock(fd, LOCK_EX) != 0) {
      if (errno == EINTR) continue;
      close(fd); return 0;
   }
   if (shmem_fstat(fd, &st) != 0) {
      int saved = errno;
      (void)shmem_flock(fd, LOCK_UN); close(fd); errno = saved;
      return 0;
   }
   if (st.st_size != 0) {
      if (st.st_size != (off_t)sizeof(record) || !read_lock_record(fd, name, &record)) {
         (void)shmem_flock(fd, LOCK_UN); close(fd); errno = EIO;
         return 0; /* Fail closed: never trust a truncated or foreign record. */
      }
      if (record.state == SHM_LOCK_INITIALIZING) {
         posix_name[0] = '/';
         snprintf(posix_name + 1, sizeof(posix_name) - 1, "%s", name);
         if (!unlink_posix_name(posix_name) || !write_lock_state(fd, name, SHM_LOCK_READY)) {
            int saved = errno;
            (void)shmem_flock(fd, LOCK_UN); close(fd); errno = saved ? saved : EIO;
            return 0;
         }
      }
   }
   *lock_fd = fd;
   return 1;
}

static void unlock_named_mapping(int fd)
{
   if (fd >= 0) { (void)shmem_flock(fd, LOCK_UN); (void)close(fd); }
}

static int make_posix_name(const char *name, char out[MAX_PATH + 2])
{
   size_t n;
   if (name == NULL || name[0] == '\0') return 0;
   n = strlen(name);
   if (n > MAX_PATH || strchr(name, '/') != NULL) return 0;
   out[0] = '/';
   memcpy(out + 1, name, n + 1);
   return 1;
}

static int mapping_protection_valid(DWORD protection)
{
   /* This port intentionally supports only data mappings. Execute, guard,
    * no-access and copy-on-write protections need separate semantics. */
   return protection == PAGE_READONLY || protection == PAGE_READWRITE;
}

static int normalize_map_access(DWORD access, DWORD *normalized)
{
   if (access == FILE_MAP_ALL_ACCESS) {
      *normalized = FILE_MAP_READ | FILE_MAP_WRITE;
      return 1;
   }
   if (access == 0 || (access & ~(FILE_MAP_READ | FILE_MAP_WRITE)) != 0)
      return 0;
   *normalized = access;
   return 1;
}

static DWORD protection_from_stat(const struct stat *st)
{
   return (st->st_mode & S_IWUSR) ? PAGE_READWRITE : PAGE_READONLY;
}

static DWORD access_from_protection(DWORD protection)
{
   return protection == PAGE_READWRITE ? (FILE_MAP_READ | FILE_MAP_WRITE) : FILE_MAP_READ;
}

static int mapping_size_from_parts(DWORD high, DWORD low, off_t *out)
{
   uint64_t value = ((uint64_t)high << 32) | (uint64_t)low;
   if (value == 0 || value > (uint64_t)INT64_MAX || (uint64_t)(off_t)value != value)
      return 0;
   *out = (off_t)value;
   return 1;
}

/* Create a mapping over a file handle registered by CreateFile. The duplicate
 * descriptor decouples the mapping lifetime from the original file handle. */
static HANDLE create_file_backed_mapping(HANDLE hFile, DWORD flProtect,
   DWORD size_high, DWORD size_low, LPCTSTR lpName)
{
   uint64_t requested = ((uint64_t)size_high << 32) | (uint64_t)size_low;
   uint64_t map_size;
   struct stat st;
   mapfile *mapping = NULL;
   struct mapfiles *entry = NULL;
   int fd = -1, readable = 0, writable = 0;
   int required_access = flProtect == PAGE_READWRITE ? 3 : 1;

   /* Cross-process named file mappings need a shared metadata protocol; do
    * not accidentally create a POSIX shm object unrelated to the backing file. */
   if (lpName != NULL && lpName[0] != '\0') {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
      return NULL;
   }
   if (!wfile_duplicate_fd(hFile, required_access, &fd, &readable, &writable))
      return NULL;
   if (shmem_fstat(fd, &st) != 0) {
      int saved = errno; close(fd);
      SetLastError(saved == EACCES ? ERROR_ACCESS_DENIED :
                   (saved == EBADF ? ERROR_INVALID_HANDLE : ERROR_GEN_FAILURE));
      return NULL;
   }
   if (!S_ISREG(st.st_mode) || st.st_size < 0) {
      close(fd); SetLastError(ERROR_FILE_INVALID); return NULL;
   }
   if (requested != 0) {
      if (requested > (uint64_t)INT64_MAX || (uint64_t)(off_t)requested != requested) {
         close(fd); SetLastError(ERROR_INVALID_PARAMETER); return NULL;
      }
      map_size = requested;
   } else {
      map_size = (uint64_t)st.st_size;
      if (map_size == 0) {
         close(fd); SetLastError(ERROR_FILE_INVALID); return NULL;
      }
   }
   /* Reserve all process-local bookkeeping before mutating the backing file.
    * Otherwise an allocation failure after ftruncate() would make
    * CreateFileMapping fail while leaving an unexpected file-size change. */
   mapping = (mapfile *)shmem_calloc(1, sizeof(*mapping));
   entry = (struct mapfiles *)shmem_calloc(1, sizeof(*entry));
   if (mapping == NULL || entry == NULL) {
      free(mapping); free(entry); close(fd);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
   }
   if (map_size > (uint64_t)st.st_size) {
      /* Windows extends a file for a larger maximum size only for a writable
       * PAGE_READWRITE mapping. A writable source handle alone must not make a
       * PAGE_READONLY mapping resize the file as a side effect. */
      if (flProtect != PAGE_READWRITE || !writable) {
         free(mapping); free(entry); close(fd);
         SetLastError(ERROR_ACCESS_DENIED);
         return NULL;
      }
      if (ftruncate(fd, (off_t)map_size) != 0) {
         int saved = errno;
         free(mapping); free(entry); close(fd);
         SetLastError(saved == EACCES || saved == EPERM || saved == EBADF ?
                      ERROR_ACCESS_DENIED :
                      (saved == ENOSPC ? ERROR_DISK_FULL : ERROR_GEN_FAILURE));
         return NULL;
      }
   }
   mapping->fd = fd;
   mapping->fsize = map_size;
   mapping->protection = flProtect;
   mapping->granted_access = access_from_protection(flProtect);
   mapping->name[0] = '\0';
   pthread_mutex_lock(&g_shmem_lock);
   entry->pMapFile = mapping;
   TAILQ_INSERT_TAIL(mapfiles_head, entry, mapfiles_entries);
   pthread_mutex_unlock(&g_shmem_lock);
   SetLastError(ERROR_SUCCESS);
   return mapping;
}

static HANDLE create_file_mapping_impl(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes,
   DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCTSTR lpName,
   int named_lock_fd)
{
   char posix_name[MAX_PATH + 2];
   off_t size;
   mapfile *mapping = NULL;
   struct mapfiles *entry = NULL;
   int fd = -1;
   int created = 0;
   struct stat existing_stat;

   if (!winapi_inited()) return NULL;
   if (lpAttributes != NULL) {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
      return NULL; /* Security descriptors / inheritance are not emulated. */
   }
   if (!mapping_protection_valid(flProtect)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
   }
   if (hFile != INVALID_HANDLE_VALUE) {
      if (hFile == NULL) {
         SetLastError(ERROR_INVALID_HANDLE);
         return NULL;
      }
      return create_file_backed_mapping(hFile, flProtect,
         dwMaximumSizeHigh, dwMaximumSizeLow, lpName);
   }
   if (!mapping_size_from_parts(dwMaximumSizeHigh, dwMaximumSizeLow, &size)) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
   }
   if (lpName != NULL && lpName[0] != '\0' && !make_posix_name(lpName, posix_name)) {
      SetLastError(ERROR_INVALID_NAME);
      return NULL;
   }

   pthread_mutex_lock(&g_shmem_lock);
   if (lpName != NULL && lpName[0] != '\0') {
      /* The outer API holds the per-name flock. Mark a missing name as being
       * initialized before O_CREAT publishes it, so a process crash at any
       * point in ftruncate/fchmod can be recovered by the next caller. */
      int probe_fd = shmem_shm_open(posix_name, O_RDONLY, 0);
      if (probe_fd >= 0) close(probe_fd);
      else if (errno == ENOENT) {
         if (named_lock_fd < 0 || !write_lock_state(named_lock_fd, lpName, SHM_LOCK_INITIALIZING)) {
            pthread_mutex_unlock(&g_shmem_lock);
            SetLastError(ERROR_GEN_FAILURE);
            return NULL;
         }
      }
      /* Every successful call gets a distinct local handle wrapper. Opening an
       * already-known name must not return the same pointer twice: otherwise
       * closing either handle invalidates the other one. */
      fd = shmem_shm_open(posix_name, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
      if (fd >= 0) created = 1;
      else if (errno == EEXIST) {
         /* Read metadata first so read-only mappings can be reopened without
          * requiring write access. Reopen writable objects with O_RDWR. */
         fd = shmem_shm_open(posix_name, O_RDONLY, 0);
         if (fd >= 0) {
            if (shmem_fstat(fd, &existing_stat) != 0) {
               int saved = errno;
               close(fd); fd = -1; errno = saved;
            } else if (existing_stat.st_size < 0) {
               close(fd); fd = -1; errno = EINVAL;
            } else if (existing_stat.st_mode & S_IWUSR) {
               int writable_fd = shmem_shm_open(posix_name, O_RDWR, 0);
               if (writable_fd >= 0) { close(fd); fd = writable_fd; }
               else { int saved = errno; close(fd); fd = -1; errno = saved; }
            }
         }
         if (fd >= 0) SetLastError(ERROR_ALREADY_EXISTS);
      }
   } else {
      /* An unnamed mapping still needs a unique POSIX shared-memory name. */
      static unsigned long serial;
      char generated[MAX_PATH + 2];
      do {
         ++serial;
         snprintf(generated, sizeof(generated), "/winapi_%ld_%lu", (long)getpid(), serial);
         fd = shmem_shm_open(generated, O_CREAT | O_EXCL | O_RDWR, S_IRUSR | S_IWUSR);
      } while (fd < 0 && errno == EEXIST);
      if (fd >= 0) {
         created = 1;
         snprintf(posix_name, sizeof(posix_name), "%s", generated);
      }
   }
   if (fd < 0) {
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(errno == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_GEN_FAILURE);
      return NULL;
   }

   if (created) {
      if (ftruncate(fd, size) != 0 ||
          shmem_fchmod(fd, flProtect == PAGE_READONLY ? S_IRUSR : (S_IRUSR | S_IWUSR)) != 0) {
         int saved = errno;
         close(fd);
         (void)unlink_posix_name(posix_name);
         pthread_mutex_unlock(&g_shmem_lock);
         SetLastError(saved == EINVAL ? ERROR_INVALID_PARAMETER : ERROR_GEN_FAILURE);
         return NULL;
      }
      if (lpName != NULL && lpName[0] != '\0') {
         if (named_lock_fd < 0 || !write_lock_state(named_lock_fd, lpName, SHM_LOCK_READY)) {
            close(fd); (void)unlink_posix_name(posix_name);
            pthread_mutex_unlock(&g_shmem_lock);
            SetLastError(ERROR_GEN_FAILURE);
            return NULL;
         }
      }
      /* Windows unnamed mappings have no discoverable name. Unlink the
       * temporary POSIX name immediately: the open fd keeps the object alive
       * for this handle and inherited/duplicated descriptors, while preventing
       * persistent /dev/shm entries after CloseHandle or process exit. */
      if (lpName == NULL || lpName[0] == '\0') {
         if (!unlink_posix_name(posix_name)) {
            close(fd);
            pthread_mutex_unlock(&g_shmem_lock);
            SetLastError(ERROR_GEN_FAILURE);
            return NULL;
         }
      }
   } else {
      /* Win32 returns the existing named mapping unchanged. The size passed
       * to this call does not resize it and may be larger or smaller than the
       * original mapping's size. */
      struct stat st;
      if (shmem_fstat(fd, &st) != 0) {
         int saved = errno;
         close(fd);
         pthread_mutex_unlock(&g_shmem_lock);
         SetLastError(saved == ENOENT ? ERROR_FILE_NOT_FOUND : ERROR_GEN_FAILURE);
         return NULL;
      }
      if (st.st_size < 0) {
         close(fd);
         pthread_mutex_unlock(&g_shmem_lock);
         SetLastError(ERROR_GEN_FAILURE);
         return NULL;
      }
   }

   mapping = (mapfile *)shmem_calloc(1, sizeof(*mapping));
   entry = (struct mapfiles *)shmem_calloc(1, sizeof(*entry));
   if (mapping == NULL || entry == NULL) {
      free(mapping); free(entry); close(fd);
      if (created) (void)unlink_posix_name(posix_name);
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
   }
   mapping->fd = fd;
   if (created) {
      mapping->fsize = (uint64_t)size;
      mapping->protection = flProtect;
   } else {
      struct stat current;
      if (shmem_fstat(fd, &current) != 0) {
         int saved = errno;
         close(fd); free(mapping); free(entry); pthread_mutex_unlock(&g_shmem_lock);
         SetLastError(saved == EACCES ? ERROR_ACCESS_DENIED : ERROR_GEN_FAILURE); return NULL;
      }
      if (current.st_size < 0) {
         close(fd); free(mapping); free(entry); pthread_mutex_unlock(&g_shmem_lock);
         SetLastError(ERROR_GEN_FAILURE); return NULL;
      }
      mapping->fsize = (uint64_t)current.st_size;
      mapping->protection = protection_from_stat(&current);
   }
   mapping->granted_access = access_from_protection(mapping->protection);
   if (lpName != NULL && lpName[0] != '\0') snprintf(mapping->name, sizeof(mapping->name), "%s", lpName);
   else snprintf(mapping->name, sizeof(mapping->name), "%.*s", (int)sizeof(mapping->name) - 1, posix_name);
   entry->pMapFile = mapping;
   TAILQ_INSERT_TAIL(mapfiles_head, entry, mapfiles_entries);
   pthread_mutex_unlock(&g_shmem_lock);
   if (!created && lpName != NULL && lpName[0] != '\0') SetLastError(ERROR_ALREADY_EXISTS);
   else SetLastError(ERROR_SUCCESS);
   return mapping;
}

HANDLE WINAPI CreateFileMapping(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes,
   DWORD flProtect, DWORD dwMaximumSizeHigh, DWORD dwMaximumSizeLow, LPCTSTR lpName)
{
   int lock_fd = -1;
   HANDLE result;
   if (hFile == INVALID_HANDLE_VALUE && lpName != NULL && lpName[0] != '\0') {
      char posix_name[MAX_PATH + 2];
      if (make_posix_name(lpName, posix_name) && !lock_named_mapping(lpName, &lock_fd)) {
         SetLastError(ERROR_GEN_FAILURE);
         return NULL;
      }
   }
   result = create_file_mapping_impl(hFile, lpAttributes, flProtect,
      dwMaximumSizeHigh, dwMaximumSizeLow, lpName, lock_fd);
   unlock_named_mapping(lock_fd);
   return result;
}

static HANDLE open_file_mapping_impl(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCTSTR lpName)
{
   char posix_name[MAX_PATH + 2];
   mapfile *mapping;
   struct mapfiles *entry;
   int fd;
   struct stat st;
   DWORD normalized_access;
   if (!winapi_inited()) return NULL;
   if (bInheritHandle) { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return NULL; }
   if (!make_posix_name(lpName, posix_name)) { SetLastError(ERROR_INVALID_NAME); return NULL; }
   if (!normalize_map_access(dwDesiredAccess, &normalized_access)) {
      SetLastError(ERROR_INVALID_PARAMETER); return NULL;
   }

   pthread_mutex_lock(&g_shmem_lock);
   /* Do not alias an existing wrapper. Each open owns its fd and registry
    * entry, matching the independent lifetime of each Win32 handle. */
   fd = shmem_shm_open(posix_name, (normalized_access & FILE_MAP_WRITE) ? O_RDWR : O_RDONLY, 0);
   if (fd < 0) {
      int saved = errno;
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(saved == ENOENT ? ERROR_FILE_NOT_FOUND :
                   (saved == EACCES ? ERROR_ACCESS_DENIED : ERROR_GEN_FAILURE));
      return NULL;
   }
   if (shmem_fstat(fd, &st) != 0) {
      int saved = errno;
      close(fd);
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(saved == EACCES ? ERROR_ACCESS_DENIED : ERROR_GEN_FAILURE);
      return NULL;
   }
   if (st.st_size < 0) {
      close(fd);
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(ERROR_GEN_FAILURE);
      return NULL;
   }
   mapping = (mapfile *)shmem_calloc(1, sizeof(*mapping));
   entry = (struct mapfiles *)shmem_calloc(1, sizeof(*entry));
   if (mapping == NULL || entry == NULL) {
      free(mapping); free(entry); close(fd); pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL;
   }
   mapping->fd = fd;
   mapping->fsize = (uint64_t)st.st_size;
   mapping->protection = protection_from_stat(&st);
   mapping->granted_access = normalized_access;
   if ((normalized_access & FILE_MAP_WRITE) && mapping->protection != PAGE_READWRITE) {
      close(fd); free(mapping); free(entry); pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(ERROR_ACCESS_DENIED); return NULL;
   }
   snprintf(mapping->name, sizeof(mapping->name), "%s", lpName);
   entry->pMapFile = mapping;
   TAILQ_INSERT_TAIL(mapfiles_head, entry, mapfiles_entries);
   pthread_mutex_unlock(&g_shmem_lock);
   return mapping;
}

HANDLE WINAPI OpenFileMapping(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCTSTR lpName)
{
   int lock_fd = -1;
   HANDLE result;
   char posix_name[MAX_PATH + 2];
   if (make_posix_name(lpName, posix_name)) {
      if (!lock_named_mapping(lpName, &lock_fd)) {
         SetLastError(ERROR_GEN_FAILURE);
         return NULL;
      }
   }
   result = open_file_mapping_impl(dwDesiredAccess, bInheritHandle, lpName);
   unlock_named_mapping(lock_fd);
   return result;
}

LPVOID WINAPI MapViewOfFile(HANDLE hFileMappingObject, DWORD dwDesiredAccess,
   DWORD dwFileOffsetHigh, DWORD dwFileOffsetLow, SIZE_T dwNumberOfBytesToMap)
{
   mapfile *mapping;
   fileview *view;
   struct fileviews *entry;
   uint64_t offset64 = ((uint64_t)dwFileOffsetHigh << 32) | dwFileOffsetLow;
   size_t size = dwNumberOfBytesToMap;
   long page_size = sysconf(_SC_PAGESIZE);
   int prot;
   DWORD normalized_access;
   void *addr;

   if (!winapi_inited() || hFileMappingObject == NULL || hFileMappingObject == INVALID_HANDLE_VALUE || page_size <= 0) {
      SetLastError(ERROR_INVALID_HANDLE); return NULL;
   }
   if (!normalize_map_access(dwDesiredAccess, &normalized_access)) {
      SetLastError(ERROR_INVALID_PARAMETER); return NULL;
   }
   prot = PROT_READ;
   if (normalized_access & FILE_MAP_WRITE) prot |= PROT_WRITE;
   if (offset64 > (uint64_t)INT64_MAX ||
       (uint64_t)(off_t)offset64 != offset64 ||
       offset64 % (uint64_t)page_size != 0) {
      SetLastError(ERROR_INVALID_PARAMETER); return NULL;
   }
   pthread_mutex_lock(&g_shmem_lock);
   mapping = NULL;
   struct mapfiles *me;
   struct stat current_stat;
   TAILQ_FOREACH(me, mapfiles_head, mapfiles_entries) if (me->pMapFile == (mapfile *)hFileMappingObject) { mapping = me->pMapFile; break; }
   if (mapping == NULL) { pthread_mutex_unlock(&g_shmem_lock); SetLastError(ERROR_INVALID_HANDLE); return NULL; }
   /* A file-backed mapping can be truncated by code outside this library.
    * mmap() may otherwise succeed and later raise SIGBUS when the view touches
    * pages beyond EOF. Detect an already-truncated backing object before
    * creating a new view. This cannot eliminate races with external truncation. */
   if (shmem_fstat(mapping->fd, &current_stat) != 0) {
      int saved = errno;
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(saved == EBADF ? ERROR_INVALID_HANDLE :
                   (saved == EACCES ? ERROR_ACCESS_DENIED : ERROR_GEN_FAILURE));
      return NULL;
   }
   if (current_stat.st_size < 0 || (uint64_t)current_stat.st_size < mapping->fsize) {
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(ERROR_FILE_INVALID);
      return NULL;
   }
   if ((normalized_access & mapping->granted_access) != normalized_access ||
       ((normalized_access & FILE_MAP_WRITE) && mapping->protection != PAGE_READWRITE)) {
      pthread_mutex_unlock(&g_shmem_lock); SetLastError(ERROR_ACCESS_DENIED); return NULL;
   }
   if (size == 0) {
      uint64_t remaining;
      if (offset64 >= mapping->fsize) { pthread_mutex_unlock(&g_shmem_lock); SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
      remaining = mapping->fsize - offset64;
      if (remaining > (uint64_t)SIZE_MAX) { pthread_mutex_unlock(&g_shmem_lock); SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
      size = (size_t)remaining;
   }
   if ((uint64_t)size > UINT64_MAX - offset64 || offset64 + (uint64_t)size > (uint64_t)mapping->fsize) {
      pthread_mutex_unlock(&g_shmem_lock); SetLastError(ERROR_INVALID_PARAMETER); return NULL;
   }
   addr = mmap(NULL, size, prot, MAP_SHARED, mapping->fd, (off_t)offset64);
   if (addr == MAP_FAILED) {
      int saved = errno;
      pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(saved == ENOMEM ? ERROR_NOT_ENOUGH_MEMORY :
                   (saved == EACCES || saved == EPERM ? ERROR_ACCESS_DENIED :
                    (saved == EINVAL ? ERROR_INVALID_PARAMETER :
                     (saved == ENODEV ? ERROR_FILE_INVALID : ERROR_GEN_FAILURE))));
      return NULL;
   }
   view = (fileview *)shmem_calloc(1, sizeof(*view));
   entry = (struct fileviews *)shmem_calloc(1, sizeof(*entry));
   if (view == NULL || entry == NULL) {
      free(view); free(entry); munmap(addr, size); pthread_mutex_unlock(&g_shmem_lock);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL;
   }
   view->shm = addr; view->shm_size = size; view->pMapFile = mapping;
   entry->pFileView = view;
   TAILQ_INSERT_TAIL(fileviews_head, entry, fileviews_entries);
   pthread_mutex_unlock(&g_shmem_lock);
   return addr;
}

/* Close a mapping only when no views still refer to it. Return a tri-state
 * result so CloseHandle can continue checking other handle registries. */
int wshmem_close_handle(HANDLE handle)
{
   struct mapfiles *entry;
   struct fileviews *view_entry;
   mapfile *mapping = NULL;
   if (!winapi_inited() || handle == NULL || handle == INVALID_HANDLE_VALUE) return 0;
   pthread_mutex_lock(&g_shmem_lock);
   TAILQ_FOREACH(entry, mapfiles_head, mapfiles_entries) {
      if (entry->pMapFile == (mapfile *)handle) { mapping = entry->pMapFile; break; }
   }
   if (mapping == NULL) { pthread_mutex_unlock(&g_shmem_lock); return 0; }
   TAILQ_FOREACH(view_entry, fileviews_head, fileviews_entries) {
      if (view_entry->pFileView != NULL && view_entry->pFileView->pMapFile == mapping) {
         pthread_mutex_unlock(&g_shmem_lock);
         SetLastError(ERROR_BUSY);
         return -1;
      }
   }
   /* On Linux close(2) may have released the descriptor even when it reports
    * EINTR. Once there are no views, retire the wrapper regardless of close's
    * return value so a possibly-closed fd cannot remain registered and be
    * reused accidentally by a later open. */
   int close_error = 0;
   if (mapping->fd >= 0 && close(mapping->fd) != 0) close_error = errno;
   mapping->fd = -1;
   TAILQ_REMOVE(mapfiles_head, entry, mapfiles_entries);
   free(mapping); free(entry);
   pthread_mutex_unlock(&g_shmem_lock);
   if (close_error != 0) {
      SetLastError(ERROR_GEN_FAILURE);
      return -1;
   }
   SetLastError(ERROR_SUCCESS);
   return 1;
}

BOOL WINAPI UnmapViewOfFile(LPCVOID lpBaseAddress)
{
   struct fileviews *entry;
   if (!winapi_inited() || lpBaseAddress == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
   pthread_mutex_lock(&g_shmem_lock);
   TAILQ_FOREACH(entry, fileviews_head, fileviews_entries) {
      fileview *view = entry->pFileView;
      if (view != NULL && view->shm == lpBaseAddress) {
         if (munmap(view->shm, view->shm_size) != 0) {
            pthread_mutex_unlock(&g_shmem_lock); SetLastError(ERROR_GEN_FAILURE); return FALSE;
         }
         TAILQ_REMOVE(fileviews_head, entry, fileviews_entries);
         free(view); free(entry);
         pthread_mutex_unlock(&g_shmem_lock);
         return TRUE;
      }
   }
   pthread_mutex_unlock(&g_shmem_lock);
   SetLastError(ERROR_INVALID_PARAMETER);
   return FALSE;
}

#endif
