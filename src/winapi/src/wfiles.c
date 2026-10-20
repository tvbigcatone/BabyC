
#include "windows.h"
#include "wprivate.h"

#include <sys/types.h>     // DIR
#include <sys/stat.h>      // stat, chmod, mkdir
#include <sys/statvfs.h>   // statvfs
#include <unistd.h>        // unlink, rmdir
#include <fcntl.h>         // AT_FDCWD
#include <dirent.h>
#include <glob.h>
#ifdef __linux__
#include <sys/syscall.h>
#endif

#ifndef max
#define max(a,b) (((a) (b)) ? (a) : (b))
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif

#define WIN_FILE_SEPARATOR     '\\'
#define POSIX_FILE_SEPARATOR   '/'

extern struct _fileSearchsHead *filesearchs_head;
/* FindFirstFile handles share the process-wide opaque HANDLE token namespace.
 * Protect lookup, iteration, insertion and removal as a single registry. */
static pthread_mutex_t g_win_find_handles_lock = PTHREAD_MUTEX_INITIALIZER;

/***********************************************************************************************/
/*                                          HELPERS                                            */
/***********************************************************************************************/

/* Translate the errno values produced by the POSIX backend into stable Win32
 * errors. Public APIs must not leave GetLastError() stale on failure. */
static DWORD winapi_error_from_errno(int error)
{
   switch (error) {
      case 0: return ERROR_SUCCESS;
      case ENOENT: case ENOTDIR: return ERROR_PATH_NOT_FOUND;
      case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
      case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
      case EMFILE: case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
      case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
      case EINVAL: return ERROR_INVALID_PARAMETER;
#ifdef EOVERFLOW
      case EOVERFLOW: return ERROR_ARITHMETIC_OVERFLOW;
#endif
      default: return ERROR_GEN_FAILURE;
   }
}

static int checked_u64_multiply(uint64_t a, uint64_t b, uint64_t *result)
{
   if (result == NULL || (b != 0 && a > UINT64_MAX / b)) return 0;
   *result = a * b;
   return 1;
}

int update_drive_list( struct _disksHead *disks_head, char resolve_root )
{
   // Check if the library is initialized.
   if (!winapi_inited()) return -1;

   int rc = 0;

   struct disks *disk_entry = NULL;

   unsigned int n = 'C';
   char mntpath[PATH_MAX];
   char resolvedpath[PATH_MAX];

   struct mntent *m;
   FILE *f;
   f = setmntent(_PATH_MOUNTED, "r");
   if (f == NULL) return -1;
   while ((m = getmntent(f)))
   {
      if (snprintf(resolvedpath, sizeof(resolvedpath), "%s", m->mnt_fsname) >= (int)sizeof(resolvedpath))
         continue;
      ssize_t len = readlink(m->mnt_fsname, mntpath, sizeof(mntpath) - 1);
      if (len != -1) 
      { 
         mntpath[len] = '\0'; 
         char *ptr;
         ptr = realpath(mntpath, resolvedpath);
         if (ptr == NULL)  
         {
            char copypath[PATH_MAX];
            const char *mnt_dirname = dirname(m->mnt_fsname);
            int written = snprintf(copypath, sizeof(copypath), "%s/%s", mnt_dirname, mntpath);
            if (written >= 0 && (size_t)written < sizeof(copypath))
               ptr = realpath(copypath, resolvedpath);
            else
               ptr = NULL;
         }
      }
    
      struct stat sb;
      if( (stat(resolvedpath, &sb) == 0 && S_ISBLK(sb.st_mode)) ||
          (strcmp(resolvedpath, "/dev/root") == 0)                ) 
      { 
         struct disks *new_disk_entry = (struct disks *)calloc(1, sizeof(*new_disk_entry));
         disk *pNewDisk = (disk *)calloc(1, sizeof(*pNewDisk));
         if (new_disk_entry == NULL || pNewDisk == NULL) {
            free(new_disk_entry);
            free(pNewDisk);
            endmntent(f);
            return -1;
         }

         if (resolve_root && strcmp(resolvedpath, "/dev/root") == 0) {
            if (!resolve_root_path(pNewDisk->device, sizeof(pNewDisk->device)) || pNewDisk->device[0] == '\0')
               snprintf(pNewDisk->device, sizeof(pNewDisk->device), "%s", resolvedpath);
         } else {
            snprintf(pNewDisk->device, sizeof(pNewDisk->device), "%s", resolvedpath);
         }
         snprintf(pNewDisk->mountpoint, sizeof(pNewDisk->mountpoint), "%s", m->mnt_dir);
         pNewDisk->drivetype = DRIVE_UNKNOWN;
      
         new_disk_entry->pDisk = pNewDisk;

         unsigned short insterted = 0;
         TAILQ_FOREACH(disk_entry, disks_head, disks_entries)
         {
            disk *pDisk = disk_entry->pDisk;
            if (strcmp(pDisk->device, pNewDisk->device) > 0)
            {
               TAILQ_INSERT_BEFORE(disk_entry, new_disk_entry, disks_entries);
               insterted = 1;
               break;
            }
         }
         if (insterted == 0) TAILQ_INSERT_TAIL(disks_head, new_disk_entry, disks_entries);
         rc++;
      }
   }
   endmntent(f);

   /* Containers and overlay filesystems often expose no block-device mount
    * entries at all. Keep the Windows drive abstraction useful there by
    * mapping the host root filesystem to the conventional C: drive. */
   if (rc == 0) {
      struct disks *root_entry = (struct disks *)calloc(1, sizeof(*root_entry));
      disk *root_disk = (disk *)calloc(1, sizeof(*root_disk));
      if (root_entry == NULL || root_disk == NULL) {
         free(root_entry);
         free(root_disk);
         return -1;
      }
      snprintf(root_disk->device, sizeof(root_disk->device), "%s", "/");
      snprintf(root_disk->mountpoint, sizeof(root_disk->mountpoint), "%s", "/");
      snprintf(root_disk->drive, sizeof(root_disk->drive), "%s", "C:");
      root_disk->drivetype = DRIVE_FIXED;
      root_entry->pDisk = root_disk;
      TAILQ_INSERT_TAIL(disks_head, root_entry, disks_entries);
      rc = 1;
   }

   TAILQ_FOREACH(disk_entry, disks_head, disks_entries) 
   {
      disk *pDisk = disk_entry->pDisk;
      if (strcmp(pDisk->mountpoint, "/") == 0) strcpy(pDisk->drive, "C:");
      else { n++; sprintf(pDisk->drive, "%c:", n); }
      // printf("Drive: %s    Device: %s      Mount Point: %s\n", pDisk->drive, pDisk->device, pDisk->mountpoint); 
   }

   return rc;

}

void clear_drive_list( struct _disksHead *disks_head )
{
   /* Cleanup must not depend on the library's initialization state: callers
    * may need to release a partially built list after initialization changes
    * or after update_drive_list() fails. */
   if (disks_head == NULL) return;

   struct disk  *pPrevDisk = NULL;
   struct disks *disk_entry, *prev_disk_entry = NULL;

   TAILQ_FOREACH_REVERSE(disk_entry, disks_head, _disksHead, disks_entries) 
   {
      if (prev_disk_entry) 
      {
         pPrevDisk = prev_disk_entry->pDisk;
         if(pPrevDisk) free(pPrevDisk);
         free(prev_disk_entry); 
      }
      TAILQ_REMOVE(disks_head, disk_entry, disks_entries);
      prev_disk_entry = disk_entry;
   }
   
   if (prev_disk_entry )
   {
      pPrevDisk = prev_disk_entry->pDisk;
      if(pPrevDisk) free(pPrevDisk);
      free(prev_disk_entry);
   } 
}

int resolve_root_path(char *resolvedpath, size_t resolvedpath_capacity)
{
   FILE *cmdline = NULL;
   char *arg = 0;
   char* token;
   char* rest;
   size_t size = 0;

   if (resolvedpath == NULL || resolvedpath_capacity == 0) {
      errno = EINVAL;
      return 0;
   }
   resolvedpath[0] = '\0';
   cmdline = fopen("/proc/cmdline", "r");
   if (cmdline)
   {
      while (getdelim(&arg, &size, 0, cmdline) != -1)
      {
         rest = arg;
         while ((token = strtok_r(rest, "= ", &rest)))
         {
            if (strcmp(token, "root") == 0)
            {
               if ((token = strtok_r(rest, "= ", &rest)))
               {
                  size_t token_len = strlen(token);
                  if (token_len >= resolvedpath_capacity) {
                     errno = ENAMETOOLONG;
                     if (arg) free(arg);
                     fclose(cmdline);
                     return 0;
                  }
                  memcpy(resolvedpath, token, token_len + 1);
                  if (arg) free(arg);
                  fclose(cmdline);
                  return 1;
               }
            }
         }
      }
      if (arg) free(arg);
      fclose(cmdline);
   }

   return 0;
}

int is_win_path(const char *path)
{
   struct disks *disk_entry = NULL;
   struct disk *pDisk = NULL;
   const char *pos = NULL;
   struct _disksHead disksHead;
   struct _disksHead *disks_head = &disksHead;

   if( path == NULL || !strlen(path) )              return 0;
   if( strchr( path, WIN_FILE_SEPARATOR ) != NULL ) return 1;

   TAILQ_INIT(disks_head);
   if (update_drive_list(disks_head, 0) < 0) {
      clear_drive_list(disks_head);
      return 0;
   }
   TAILQ_FOREACH(disk_entry, disks_head, disks_entries) 
   {
      pDisk = disk_entry->pDisk;
      pos = strcasestr(path, (const char *)pDisk->drive);
      if (pos == path)
      {
         clear_drive_list(disks_head); 
         return 1;
      }
   }
   clear_drive_list(disks_head);

   return 0;
}

char *win2posix_path(const char *win_path)
{
   struct disks *entry;
   struct _disksHead head;
   char *result = NULL;
   size_t i, length;

   if (win_path == NULL) { errno = EINVAL; return NULL; }
   length = strlen(win_path);
   if (length == SIZE_MAX) { errno = ENAMETOOLONG; return NULL; }

   /* First translate a known drive prefix. Match only the beginning of the
      path and never search for a drive letter in an arbitrary component. */
   TAILQ_INIT(&head);
   if (winapi_inited() && update_drive_list(&head, 0) >= 0) {
      TAILQ_FOREACH(entry, &head, disks_entries) {
         disk *d = entry->pDisk;
         size_t drive_len, mount_len, suffix_len;
         const char *suffix;
         if (d == NULL) continue;
         drive_len = strlen(d->drive);
         if (drive_len != 2 || strncasecmp(win_path, d->drive, 2) != 0) continue;
         if (win_path[2] != '\\' && win_path[2] != '/' && win_path[2] != '\0' && win_path[2] != '.')
            continue;

         mount_len = strlen(d->mountpoint);
         suffix = win_path + 2;
         if (*suffix == '.') suffix++;
         while (*suffix == '\\' || *suffix == '/') suffix++;
         suffix_len = strlen(suffix);
         if (mount_len > SIZE_MAX - suffix_len - 2) { errno = ENAMETOOLONG; break; }
         result = malloc(mount_len + suffix_len + 2);
         if (result == NULL) { errno = ENOMEM; break; }
         memcpy(result, d->mountpoint, mount_len);
         if (suffix_len != 0) {
            size_t offset = mount_len;
            if (offset == 0 || result[offset - 1] != '/') result[offset++] = '/';
            memcpy(result + offset, suffix, suffix_len);
            result[offset + suffix_len] = '\0';
         } else {
            if (mount_len > 1 && result[mount_len - 1] == '/') result[mount_len - 1] = '\0';
            else result[mount_len] = '\0';
         }
         break;
      }
   }
   clear_drive_list(&head);
   if (result == NULL) {
      result = strdup(win_path);
      if (result == NULL) { errno = ENOMEM; return NULL; }
      /* A rooted Windows path without a resolvable drive is kept relative to
         the current directory, matching the historical library convention. */
      if (result[0] == '\\') {
         char *rooted = malloc(length + 3);
         if (rooted == NULL) { free(result); errno = ENOMEM; return NULL; }
         rooted[0] = '.'; rooted[1] = '/';
         memcpy(rooted + 2, result, length + 1);
         free(result);
         result = rooted;
      }
   }
   for (i = 0; result[i] != '\0'; ++i) if (result[i] == '\\') result[i] = '/';
   /* Collapse duplicate separators except the initial //, which can be
      meaningful on POSIX systems. */
   {
      size_t read_pos = 0, write_pos = 0;
      while (result[read_pos] != '\0') {
         char c = result[read_pos++];
         if (c == '/' && write_pos > 0 && result[write_pos - 1] == '/' &&
             !(write_pos == 1 && result[0] == '/')) continue;
         result[write_pos++] = c;
      }
      result[write_pos] = '\0';
   }
   return result;
}

char *posix2win_path(const char *posix_path)
{
   struct disks *entry;
   struct _disksHead head;
   disk *root_disk = NULL;
   char *result = NULL;
   size_t path_len;

   if (posix_path == NULL) { errno = EINVAL; return NULL; }
   path_len = strlen(posix_path);
   TAILQ_INIT(&head);
   if (winapi_inited() && update_drive_list(&head, 0) >= 0) {
      TAILQ_FOREACH(entry, &head, disks_entries) {
         disk *d = entry->pDisk;
         size_t mount_len;
         if (d == NULL) continue;
         if (strcmp(d->mountpoint, "/") == 0) { root_disk = d; continue; }
         mount_len = strlen(d->mountpoint);
         if (strncmp(posix_path, d->mountpoint, mount_len) == 0 &&
             (mount_len == 1 || d->mountpoint[mount_len - 1] == '/' ||
              posix_path[mount_len] == '\0' || posix_path[mount_len] == '/')) {
            const char *suffix = posix_path + mount_len;
            size_t drive_len = strlen(d->drive), suffix_len = strlen(suffix);
            size_t extra_sep = (suffix_len == 0 || *suffix != '/') ? 1 : 0;
            if (drive_len > SIZE_MAX - suffix_len - extra_sep - 1) { errno = ENAMETOOLONG; break; }
            result = malloc(drive_len + suffix_len + extra_sep + 1);
            if (result == NULL) { errno = ENOMEM; break; }
            memcpy(result, d->drive, drive_len);
            size_t off = drive_len;
            if (extra_sep) result[off++] = '\\';
            else if (suffix_len > 0) { suffix++; suffix_len--; }
            memcpy(result + off, suffix, suffix_len);
            result[off + suffix_len] = '\0';
            break;
         }
      }
      if (result == NULL && posix_path[0] == '/' && root_disk != NULL) {
         size_t drive_len = strlen(root_disk->drive);
         if (drive_len <= SIZE_MAX - path_len - 1) {
            result = malloc(drive_len + path_len + 1);
            if (result != NULL) {
               memcpy(result, root_disk->drive, drive_len);
               size_t off = drive_len;
               if (path_len == 1) result[off++] = '\\';
               else { result[off++] = '\\'; memcpy(result + off, posix_path + 1, path_len - 1); off += path_len - 1; }
               result[off] = '\0';
            } else errno = ENOMEM;
         } else errno = ENAMETOOLONG;
      }
   }
   clear_drive_list(&head);
   if (result == NULL && posix_path[0] != '/') {
      result = strdup(posix_path);
      if (result == NULL) errno = ENOMEM;
   }
   if (result != NULL) {
      size_t i;
      for (i = 0; result[i] != '\0'; ++i) if (result[i] == '/') result[i] = '\\';
   }
   return result;
}

/***********************************************************************************************/
/*                                     PATH MANIPULATION                                       */
/***********************************************************************************************/

DWORD WINAPI GetCurrentDirectory(DWORD nBufferLength, LPTSTR lpBuffer)
{
   char *cwd = getcwd(NULL, 0);
   char *win_path;
   size_t required;

   if (cwd == NULL) {
      int saved = errno;
      SetLastError(winapi_error_from_errno(saved));
      return 0;
   }
   win_path = posix2win_path(cwd);
   if (win_path == NULL) {
      /* In container/minimal environments no root drive may be discoverable. */
      win_path = strdup(cwd);
   }
   free(cwd);
   if (win_path == NULL) {
      errno = ENOMEM;
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return 0;
   }
   required = strlen(win_path) + 1;
   if (required > UINT32_MAX) {
      free(win_path);
      SetLastError(ERROR_FILENAME_EXCED_RANGE);
      return 0;
   }
   if (lpBuffer == NULL || nBufferLength == 0 || (size_t)nBufferLength < required) {
      free(win_path);
      /* Win32 returns the required capacity, including the NUL, when the
       * supplied buffer is too small. */
      SetLastError(ERROR_INSUFFICIENT_BUFFER);
      return (DWORD)required;
   }
   memcpy(lpBuffer, win_path, required);
   free(win_path);
   SetLastError(ERROR_SUCCESS);
   return (DWORD)(required - 1);
}

BOOL WINAPI SetCurrentDirectory(LPCTSTR lpPathName)
{
   if (lpPathName == NULL) {
      errno = EINVAL;
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (win_chdir(lpPathName) == 0) {
      SetLastError(ERROR_SUCCESS);
      return TRUE;
   }
   {
      int saved = errno;
      SetLastError(winapi_error_from_errno(saved));
      return FALSE;
   }
}

struct filesearch;
struct filesearchs; // filesearchs queue

// GLOB_APPEND
//    Append found pathnames to the ones from a previous call from glob().
// GLOB_DOOFFS
//    Use the value in pglob->gl_offs to specify how many NULL pointers to add at the beginning of pglob->pathv. After the call to glob(), pglob->pathv will contain pglob->gl_offs NULL pointers, followed by pglob->gl_pathc pathnames, followed by a NULL pointer. This can be useful if you're building a command to be applied to the matched files.
// GLOB_ERR
//    Cause glob() to return when it encounters a directory that it can't open or read. Otherwise, glob() will continue to find matches.
// GLOB_MARK
//    Append a slash to each matching pathname that's a directory.
// GLOB_NOCHECK
//    If pattern doesn't match any path names, return only the contents of pattern.
// GLOB_NOESCAPE
//    Disable backslash escapes in pattern.
// GLOB_NOSORT
//    Don't sort the returned pathnames; they'll appear in an arbitrary order. The default is to sort the pathnames.

static DWORD winapi_attributes_from_stat(const struct stat *st, const char *path);

static void set_find_last_error_from_errno(int error)
{
   switch (error) {
      case ENOENT: SetLastError(ERROR_FILE_NOT_FOUND); break;
      case ENOTDIR: SetLastError(ERROR_PATH_NOT_FOUND); break;
      case EACCES: case EPERM: case EROFS: SetLastError(ERROR_ACCESS_DENIED); break;
      case ENOMEM: SetLastError(ERROR_NOT_ENOUGH_MEMORY); break;
      case EMFILE: case ENFILE: SetLastError(ERROR_TOO_MANY_OPEN_FILES); break;
      case ENAMETOOLONG: SetLastError(ERROR_FILENAME_EXCED_RANGE); break;
      case EINVAL: SetLastError(ERROR_INVALID_PARAMETER); break;
#ifdef EOVERFLOW
      case EOVERFLOW: SetLastError(ERROR_INVALID_DATA); break;
#endif
      case EBADF: SetLastError(ERROR_INVALID_HANDLE); break;
      default: SetLastError(ERROR_GEN_FAILURE); break;
   }
}

static int copy_find_filename(LPWIN32_FIND_DATA data, const char *path)
{
   const char *end, *name;
   size_t length;
   struct stat st;
   if (data == NULL || path == NULL) { errno = EINVAL; return 0; }

   /* glob(GLOB_MARK) appends '/' to directory results. Trim those separators
    * for the basename only; retain the original path for stat(). Without this,
    * a directory wildcard search returned an empty cFileName for every directory. */
   end = path + strlen(path);
   while (end > path + 1 && end[-1] == '/') --end;
   name = end;
   while (name > path && name[-1] != '/') --name;
   length = (size_t)(end - name);
   if (length >= sizeof(data->cFileName)) { errno = ENAMETOOLONG; return 0; }

   /* A result that disappeared between glob() and stat() is not a valid
    * metadata record. Fail instead of returning success with zero attributes
    * and timestamps that look like a real file. */
   if (stat(path, &st) != 0) return 0;
   if (st.st_size < 0) { errno = EOVERFLOW; return 0; }

   memset(data, 0, sizeof(*data));
   memcpy(data->cFileName, name, length);
   data->cFileName[length] = '\0';
   data->dwFileAttributes = winapi_attributes_from_stat(&st, path);
   data->nFileSizeHigh = (DWORD)(((uint64_t)st.st_size) >> 32);
   data->nFileSizeLow = (DWORD)((uint64_t)st.st_size & UINT32_MAX);
   /* st_ctim is only a documented proxy for creation on POSIX platforms
    * without a portable birth-time field. Access/write times preserve the
    * available nanosecond precision, truncated to 100ns FILETIME ticks. */
   if (!winapi_timespec_to_filetime(&st.st_ctim, &data->ftCreationTime) ||
       !winapi_timespec_to_filetime(&st.st_atim, &data->ftLastAccessTime) ||
       !winapi_timespec_to_filetime(&st.st_mtim, &data->ftLastWriteTime)) {
      errno = EOVERFLOW;
      return 0;
   }
   return 1;
}

/* Return the attributes shared by GetFileAttributes[Ex] and FindFirstFile.
 * POSIX has no portable Windows hidden/system/archive flags; dot-files are
 * treated as hidden and ordinary files as archive. */
static DWORD winapi_attributes_from_stat(const struct stat *st, const char *path)
{
   DWORD attributes = 0;
   const char *end, *base;
   size_t base_len;
   if (S_ISDIR(st->st_mode)) attributes |= FILE_ATTRIBUTE_DIRECTORY;
   else if (S_ISREG(st->st_mode)) attributes |= FILE_ATTRIBUTE_ARCHIVE;
   else attributes |= FILE_ATTRIBUTE_DEVICE;
   if ((st->st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0)
      attributes |= FILE_ATTRIBUTE_READONLY;
   end = path + strlen(path);
   while (end > path + 1 && end[-1] == '/') --end;
   base = end;
   while (base > path && base[-1] != '/') --base;
   base_len = (size_t)(end - base);
   if (base_len > 1 && base[0] == '.' &&
       !(base_len == 2 && base[1] == '.'))
      attributes |= FILE_ATTRIBUTE_HIDDEN;
   if (attributes == 0) attributes = FILE_ATTRIBUTE_NORMAL;
   return attributes;
}

static int winapi_resolve_path(LPCTSTR filename, char **converted, const char **path)
{
   if (converted == NULL || path == NULL) { errno = EINVAL; return -1; }
   *converted = NULL;
   *path = NULL;
   if (filename == NULL || filename[0] == '\0') { errno = EINVAL; return -1; }
   if (is_win_path(filename)) {
      *converted = win2posix_path(filename);
      if (*converted == NULL) {
         if (errno == 0) errno = ENOMEM;
         return -1;
      }
      *path = *converted;
   } else {
      *path = filename;
   }
   return 0;
}

static int winapi_stat_path(LPCTSTR filename, char **converted, const char **path,
                            struct stat *st)
{
   if (winapi_resolve_path(filename, converted, path) != 0) return -1;
   if (stat(*path, st) != 0) {
      int saved = errno;
      free(*converted);
      *converted = NULL;
      *path = NULL;
      errno = saved;
      return -1;
   }
   return 0;
}

static void winapi_set_error_from_errno(int error)
{
   switch (error) {
      case 0: SetLastError(ERROR_SUCCESS); break;
      case ENOENT: SetLastError(ERROR_FILE_NOT_FOUND); break;
      case ENOTDIR: SetLastError(ERROR_PATH_NOT_FOUND); break;
      case EACCES: case EPERM: case EROFS: SetLastError(ERROR_ACCESS_DENIED); break;
      case EEXIST: SetLastError(ERROR_ALREADY_EXISTS); break;
      case ENAMETOOLONG: SetLastError(ERROR_FILENAME_EXCED_RANGE); break;
      case ENOMEM: SetLastError(ERROR_NOT_ENOUGH_MEMORY); break;
      case EMFILE: case ENFILE: SetLastError(ERROR_TOO_MANY_OPEN_FILES); break;
      case ENOSPC: SetLastError(ERROR_DISK_FULL); break;
#ifdef EDQUOT
      case EDQUOT: SetLastError(ERROR_DISK_FULL); break;
#endif
      case EISDIR: SetLastError(ERROR_ACCESS_DENIED); break;
      case ENOTEMPTY: SetLastError(ERROR_DIR_NOT_EMPTY); break;
#ifdef EXDEV
      case EXDEV: SetLastError(ERROR_NOT_SAME_DEVICE); break;
#endif
      case EINVAL: SetLastError(ERROR_INVALID_PARAMETER); break;
#ifdef EOVERFLOW
      case EOVERFLOW: SetLastError(ERROR_ARITHMETIC_OVERFLOW); break;
#endif
      default: SetLastError(ERROR_GEN_FAILURE); break;
   }
}

BOOL WINAPI DeleteFile(LPCTSTR lpFileName)
{
   char *converted;
   const char *path;
   struct stat st;
   if (winapi_resolve_path(lpFileName, &converted, &path) != 0) {
      winapi_set_error_from_errno(errno); return FALSE;
   }
   if (lstat(path, &st) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   if (S_ISDIR(st.st_mode) ||
       (st.st_mode & (S_IWUSR | S_IWGRP | S_IWOTH)) == 0) {
      free(converted); SetLastError(ERROR_ACCESS_DENIED); return FALSE;
   }
   if (unlink(path) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   free(converted); SetLastError(ERROR_SUCCESS); return TRUE;
}

#ifndef MOVEFILE_REPLACE_EXISTING
#define MOVEFILE_REPLACE_EXISTING 0x00000001u
#endif
#ifndef MOVEFILE_COPY_ALLOWED
#define MOVEFILE_COPY_ALLOWED     0x00000002u
#endif
#ifndef MOVEFILE_DELAY_UNTIL_REBOOT
#define MOVEFILE_DELAY_UNTIL_REBOOT 0x00000004u
#endif
#ifndef MOVEFILE_WRITE_THROUGH
#define MOVEFILE_WRITE_THROUGH   0x00000008u
#endif

static int winapi_rename_noreplace(const char *old_path, const char *new_path)
{
#if defined(__linux__) && defined(SYS_renameat2)
   /* Linux can make the no-replace guarantee atomic even against other
    * processes. Fall back only when the kernel/filesystem lacks the feature. */
   if (syscall(SYS_renameat2, AT_FDCWD, old_path, AT_FDCWD, new_path, 1u) == 0)
      return 0;
   if (errno != ENOSYS && errno != EINVAL && errno != EOPNOTSUPP) return -1;
#endif
   {
      struct stat st;
      if (lstat(new_path, &st) == 0) { errno = EEXIST; return -1; }
      if (errno != ENOENT) return -1;
   }
   /* Portable fallback has an unavoidable check/rename race on POSIX systems
    * without an atomic no-replace rename primitive. */
   return rename(old_path, new_path);
}

static int winapi_sync_parent_directory(const char *path)
{
   char *parent;
   const char *slash = strrchr(path, '/');
   size_t len;
   int fd, saved;
   if (slash == NULL) parent = strdup(".");
   else if (slash == path) parent = strdup("/");
   else {
      len = (size_t)(slash - path);
      parent = (char *)malloc(len + 1);
      if (parent != NULL) { memcpy(parent, path, len); parent[len] = '\0'; }
   }
   if (parent == NULL) { errno = ENOMEM; return -1; }
   fd = open(parent, O_RDONLY
#ifdef O_DIRECTORY
             | O_DIRECTORY
#endif
#ifdef O_CLOEXEC
             | O_CLOEXEC
#endif
             );
   saved = errno;
   free(parent);
   if (fd < 0) { errno = saved; return -1; }
   if (fsync(fd) != 0) { saved = errno; (void)close(fd); errno = saved; return -1; }
   if (close(fd) != 0) return -1;
   return 0;
}

BOOL WINAPI MoveFileEx(LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName,
                       DWORD dwFlags)
{
   char *old_converted = NULL, *new_converted = NULL;
   const char *old_path = NULL, *new_path = NULL;
   struct stat source_st;
   const DWORD supported = MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED |
                          MOVEFILE_DELAY_UNTIL_REBOOT | MOVEFILE_WRITE_THROUGH;
   int rc, saved;
   if ((dwFlags & ~supported) != 0) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
   if ((dwFlags & MOVEFILE_DELAY_UNTIL_REBOOT) != 0) {
      SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
   }
   if (winapi_resolve_path(lpExistingFileName, &old_converted, &old_path) != 0 ||
       winapi_resolve_path(lpNewFileName, &new_converted, &new_path) != 0) {
      saved = errno ? errno : EINVAL; free(old_converted); free(new_converted);
      winapi_set_error_from_errno(saved); return FALSE;
   }
   if (lstat(old_path, &source_st) != 0) {
      saved = errno; free(old_converted); free(new_converted);
      winapi_set_error_from_errno(saved); return FALSE;
   }
   if ((dwFlags & MOVEFILE_REPLACE_EXISTING) != 0)
      rc = rename(old_path, new_path);
   else
      rc = winapi_rename_noreplace(old_path, new_path);
   if (rc != 0) {
      saved = errno;
      if (saved == EXDEV && (dwFlags & MOVEFILE_COPY_ALLOWED) != 0 &&
          S_ISREG(source_st.st_mode)) {
         BOOL copied = CopyFile(old_path, new_path,
             (dwFlags & MOVEFILE_REPLACE_EXISTING) == 0 ? TRUE : FALSE);
         if (copied) {
            if (unlink(old_path) == 0) {
               rc = 0;
            } else {
               saved = errno;
               /* Copy is complete but source removal failed; report the real
                * failure and leave both copies rather than deleting data. */
               free(old_converted); free(new_converted);
               winapi_set_error_from_errno(saved); return FALSE;
            }
         } else {
            saved = (int)GetLastError();
            free(old_converted); free(new_converted);
            SetLastError((DWORD)saved); return FALSE;
         }
      } else {
         free(old_converted); free(new_converted);
         winapi_set_error_from_errno(saved); return FALSE;
      }
   }
   if ((dwFlags & MOVEFILE_WRITE_THROUGH) != 0) {
      /* A rename changes directory entries in both directories when the
       * source and destination have different parents. Syncing only the
       * destination is insufficient for the WRITE_THROUGH contract: after a
       * crash, the new name could persist while removal of the old name does
       * not (or vice versa). Sync both parents. If the second sync fails,
       * the rename has already committed and cannot be rolled back safely. */
      if (winapi_sync_parent_directory(new_path) != 0 ||
          winapi_sync_parent_directory(old_path) != 0) {
         saved = errno;
         free(old_converted); free(new_converted);
         winapi_set_error_from_errno(saved); return FALSE;
      }
   }
   free(old_converted); free(new_converted); SetLastError(ERROR_SUCCESS); return TRUE;
}

BOOL WINAPI MoveFile(LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName)
{
   return MoveFileEx(lpExistingFileName, lpNewFileName, 0);
}

/* Copy to a private sibling and publish only after the complete copy has
 * reached disk. This avoids exposing partially written files and makes the
 * no-overwrite variant race-safe (link() is the commit operation). */
BOOL WINAPI CopyFile(LPCTSTR lpExistingFileName, LPCTSTR lpNewFileName,
                     BOOL bFailIfExists)
{
   char *src_converted = NULL, *dst_converted = NULL;
   const char *src_path = NULL, *dst_path = NULL;
   struct stat src_st, dst_st;
   int src_fd = -1, tmp_fd = -1, saved = 0, committed = 0;
   char *tmp_path = NULL;
   const char *slash;
   char *parent = NULL;
   char buffer[64 * 1024];

   if (winapi_resolve_path(lpExistingFileName, &src_converted, &src_path) != 0 ||
       winapi_resolve_path(lpNewFileName, &dst_converted, &dst_path) != 0) {
      saved = errno ? errno : EINVAL;
      free(src_converted); free(dst_converted);
      winapi_set_error_from_errno(saved); return FALSE;
   }
   src_fd = open(src_path, O_RDONLY
#ifdef O_CLOEXEC
                 | O_CLOEXEC
#endif
                 );
   if (src_fd < 0) { saved = errno; goto fail; }
   if (fstat(src_fd, &src_st) != 0) { saved = errno; goto fail; }
   if (!S_ISREG(src_st.st_mode)) { saved = S_ISDIR(src_st.st_mode) ? EISDIR : EINVAL; goto fail; }
   if (stat(dst_path, &dst_st) == 0) {
      if (src_st.st_dev == dst_st.st_dev && src_st.st_ino == dst_st.st_ino) {
         saved = EINVAL; goto fail;
      }
   } else if (errno != ENOENT) {
      saved = errno; goto fail;
   }

   slash = strrchr(dst_path, '/');
   if (slash == NULL) parent = strdup(".");
   else if (slash == dst_path) parent = strdup("/");
   else {
      size_t parent_len = (size_t)(slash - dst_path);
      parent = (char *)malloc(parent_len + 1);
      if (parent != NULL) { memcpy(parent, dst_path, parent_len); parent[parent_len] = '\0'; }
   }
   if (parent == NULL) { saved = ENOMEM; goto fail; }
   if (strlen(parent) > SIZE_MAX - sizeof("/.babyc-copy-XXXXXX")) { saved = ENAMETOOLONG; goto fail; }
   tmp_path = (char *)malloc(strlen(parent) + sizeof("/.babyc-copy-XXXXXX"));
   if (tmp_path == NULL) { saved = ENOMEM; goto fail; }
   if (snprintf(tmp_path, strlen(parent) + sizeof("/.babyc-copy-XXXXXX"),
                "%s%s.babyc-copy-XXXXXX", parent,
                strcmp(parent, "/") == 0 ? "" : "/") < 0) {
      saved = EINVAL; goto fail;
   }
   tmp_fd = mkstemp(tmp_path);
   if (tmp_fd < 0) { saved = errno; goto fail; }
#ifdef FD_CLOEXEC
   (void)fcntl(tmp_fd, F_SETFD, FD_CLOEXEC);
#endif
   for (;;) {
      ssize_t nread = read(src_fd, buffer, sizeof(buffer));
      if (nread == 0) break;
      if (nread < 0) { if (errno == EINTR) continue; saved = errno; goto fail; }
      ssize_t offset = 0;
      while (offset < nread) {
         ssize_t nwritten = write(tmp_fd, buffer + offset, (size_t)(nread - offset));
         if (nwritten < 0) { if (errno == EINTR) continue; saved = errno; goto fail; }
         if (nwritten == 0) { saved = EIO; goto fail; }
         offset += nwritten;
      }
   }
   if (fchmod(tmp_fd, src_st.st_mode & 0777) != 0) { saved = errno; goto fail; }
   /* Preserve source timestamps where supported; failure is not fatal because
    * filesystems may reject timestamp restoration for otherwise writable files. */
#if defined(_POSIX_VERSION)
   {
      struct timespec times[2];
      times[0] = src_st.st_atim;
      times[1] = src_st.st_mtim;
      (void)futimens(tmp_fd, times);
   }
#endif
   if (fsync(tmp_fd) != 0) { saved = errno; goto fail; }
   if (close(tmp_fd) != 0) { tmp_fd = -1; saved = errno; goto fail; }
   tmp_fd = -1;
   if (close(src_fd) != 0) { src_fd = -1; saved = errno; goto fail; }
   src_fd = -1;

   if (bFailIfExists) {
      if (link(tmp_path, dst_path) != 0) { saved = errno; goto fail; }
      committed = 1;
      if (unlink(tmp_path) != 0) { saved = errno; goto fail; }
   } else {
      if (rename(tmp_path, dst_path) != 0) { saved = errno; goto fail; }
      committed = 1;
   }
   free(tmp_path); free(parent); free(src_converted); free(dst_converted);
   SetLastError(ERROR_SUCCESS); return TRUE;

fail:
   if (tmp_fd >= 0) (void)close(tmp_fd);
   if (src_fd >= 0) (void)close(src_fd);
   if (tmp_path != NULL) (void)unlink(tmp_path);
   /* If link() succeeded but unlinking the temporary name failed, destination
    * is nevertheless valid; report success rather than claim rollback. */
   if (committed) {
      free(tmp_path); free(parent); free(src_converted); free(dst_converted);
      SetLastError(ERROR_SUCCESS); return TRUE;
   }
   free(tmp_path); free(parent); free(src_converted); free(dst_converted);
   winapi_set_error_from_errno(saved ? saved : EIO);
   return FALSE;
}

BOOL WINAPI CreateDirectory(LPCTSTR lpPathName, LPSECURITY_ATTRIBUTES lpSecurityAttributes)
{
   char *converted;
   const char *path;
   if (lpSecurityAttributes != NULL) { SetLastError(ERROR_NOT_SUPPORTED); return FALSE; }
   if (winapi_resolve_path(lpPathName, &converted, &path) != 0) {
      winapi_set_error_from_errno(errno); return FALSE;
   }
   if (posix_mkdir(path) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   free(converted); SetLastError(ERROR_SUCCESS); return TRUE;
}

BOOL WINAPI RemoveDirectory(LPCTSTR lpPathName)
{
   char *converted;
   const char *path;
   if (winapi_resolve_path(lpPathName, &converted, &path) != 0) {
      winapi_set_error_from_errno(errno); return FALSE;
   }
   if (posix_rmdir(path) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   free(converted); SetLastError(ERROR_SUCCESS); return TRUE;
}

BOOL WINAPI SetFileAttributes(LPCTSTR lpFileName, DWORD dwFileAttributes)
{
   char *converted = NULL;
   const char *path = NULL;
   struct stat st;
   DWORD current, requested = dwFileAttributes;
   mode_t mode;
   const DWORD supported = FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN |
                           FILE_ATTRIBUTE_ARCHIVE | FILE_ATTRIBUTE_DIRECTORY |
                           FILE_ATTRIBUTE_NORMAL | FILE_ATTRIBUTE_DEVICE;
   if (requested == 0 || (requested & ~supported) != 0 ||
       ((requested & FILE_ATTRIBUTE_NORMAL) != 0 && requested != FILE_ATTRIBUTE_NORMAL)) {
      SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
   }
   if (winapi_resolve_path(lpFileName, &converted, &path) != 0) {
      winapi_set_error_from_errno(errno); return FALSE;
   }
   /* chmod() follows symbolic links. Silently using stat()+chmod() here would
    * modify the target's permissions instead of the link named by the caller.
    * This backend cannot set Windows link attributes, so fail closed. */
   if (lstat(path, &st) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   if (S_ISLNK(st.st_mode)) {
      free(converted); SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
   }
   current = winapi_attributes_from_stat(&st, path);
   /* Hidden is a name-derived POSIX convention, not a mutable inode flag. */
   if (((requested & FILE_ATTRIBUTE_HIDDEN) != 0) !=
       ((current & FILE_ATTRIBUTE_HIDDEN) != 0)) {
      free(converted); SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
   }
   /* The POSIX projection marks ordinary files as ARCHIVE, but does not store
    * a mutable archive bit. Do not report success when a request would set or
    * clear a bit that this backend cannot actually persist (FILE_ATTRIBUTE_NORMAL
    * on a regular file also requests clearing the projected ARCHIVE bit). */
   if (((requested & FILE_ATTRIBUTE_ARCHIVE) != 0) !=
       ((current & FILE_ATTRIBUTE_ARCHIVE) != 0)) {
      free(converted); SetLastError(ERROR_NOT_SUPPORTED); return FALSE;
   }
   if (((requested & FILE_ATTRIBUTE_DIRECTORY) != 0) != S_ISDIR(st.st_mode) ||
       ((requested & FILE_ATTRIBUTE_DEVICE) != 0) !=
          (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode))) {
      free(converted); SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
   }
   mode = st.st_mode & (mode_t)07777;
   if ((requested & FILE_ATTRIBUTE_READONLY) != 0)
      mode &= (mode_t)~(S_IWUSR | S_IWGRP | S_IWOTH);
   else
      mode |= S_IWUSR;
   if (chmod(path, mode) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   free(converted); SetLastError(ERROR_SUCCESS); return TRUE;
}

DWORD WINAPI GetFileAttributes(LPCTSTR lpFileName)
{
   char *converted;
   const char *path;
   struct stat st;
   DWORD attributes;
   if (winapi_stat_path(lpFileName, &converted, &path, &st) != 0) {
      int saved = errno;
      SetLastError(saved == ENOENT ? ERROR_FILE_NOT_FOUND :
                   saved == ENOTDIR ? ERROR_PATH_NOT_FOUND : winapi_error_from_errno(saved));
      return INVALID_FILE_ATTRIBUTES;
   }
   attributes = winapi_attributes_from_stat(&st, path);
   free(converted);
   SetLastError(ERROR_SUCCESS);
   return attributes;
}

BOOL WINAPI GetFileAttributesEx(LPCTSTR lpFileName,
   GET_FILEEX_INFO_LEVELS fInfoLevelId, LPVOID lpFileInformation)
{
   char *converted;
   const char *path;
   struct stat st;
   WIN32_FILE_ATTRIBUTE_DATA result;
   uint64_t size;
   if (fInfoLevelId != GetFileExInfoStandard || lpFileInformation == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   if (winapi_stat_path(lpFileName, &converted, &path, &st) != 0) {
      int saved = errno;
      SetLastError(saved == ENOENT ? ERROR_FILE_NOT_FOUND :
                   saved == ENOTDIR ? ERROR_PATH_NOT_FOUND : winapi_error_from_errno(saved));
      return FALSE;
   }
   memset(&result, 0, sizeof(result));
   result.dwFileAttributes = winapi_attributes_from_stat(&st, path);
   if (!winapi_timespec_to_filetime(&st.st_ctim, &result.ftCreationTime) ||
       !winapi_timespec_to_filetime(&st.st_atim, &result.ftLastAccessTime) ||
       !winapi_timespec_to_filetime(&st.st_mtim, &result.ftLastWriteTime)) {
      free(converted);
      SetLastError(ERROR_ARITHMETIC_OVERFLOW);
      return FALSE;
   }
   size = S_ISDIR(st.st_mode) ? 0u : (st.st_size < 0 ? UINT64_MAX : (uint64_t)st.st_size);
   if (size == UINT64_MAX) {
      free(converted);
      SetLastError(ERROR_INVALID_DATA);
      return FALSE;
   }
   result.nFileSizeHigh = (DWORD)(size >> 32);
   result.nFileSizeLow = (DWORD)(size & UINT32_MAX);
   free(converted);
   memcpy(lpFileInformation, &result, sizeof(result));
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

DWORD WINAPI GetTempPath(DWORD nBufferLength, LPTSTR lpBuffer)
{
   const char *candidate = getenv("TMPDIR");
   struct stat st;
   char fallback[PATH_MAX];
   size_t len;
   if (candidate == NULL || candidate[0] == '\0' || stat(candidate, &st) != 0 || !S_ISDIR(st.st_mode))
      candidate = "/tmp";
   if (stat(candidate, &st) != 0 || !S_ISDIR(st.st_mode)) candidate = "/";
   len = strlen(candidate);
   if (len == 0) candidate = "/", len = 1;
   if (candidate[len - 1] != '/') {
      if (len + 1 >= sizeof(fallback)) { SetLastError(ERROR_FILENAME_EXCED_RANGE); return 0; }
      memcpy(fallback, candidate, len);
      fallback[len++] = '/';
      fallback[len] = '\0';
      candidate = fallback;
   }
   if (len >= UINT32_MAX) { SetLastError(ERROR_ARITHMETIC_OVERFLOW); return 0; }
   if (lpBuffer == NULL) {
      if (nBufferLength != 0) { SetLastError(ERROR_INVALID_PARAMETER); return 0; }
      SetLastError(ERROR_SUCCESS);
      return (DWORD)(len + 1); /* required size when no buffer was supplied */
   }
   if ((size_t)nBufferLength <= len) {
      if (nBufferLength > 0) lpBuffer[0] = '\0';
      SetLastError(ERROR_BUFFER_OVERFLOW);
      return (DWORD)(len + 1);
   }
   memcpy(lpBuffer, candidate, len);
   lpBuffer[len] = '\0';
   SetLastError(ERROR_SUCCESS);
   return (DWORD)len;
}

UINT WINAPI GetTempFileName(LPCTSTR lpPathName, LPCTSTR lpPrefixString,
                            UINT uUnique, LPTSTR lpTempFileName)
{
   char *converted = NULL;
   const char *directory = NULL;
   struct stat st;
   const char *prefix = lpPrefixString ? lpPrefixString : "";
   size_t prefix_len = strlen(prefix);
   size_t dir_len;
   char path[MAX_PATH];
   unsigned int first, tries;
   size_t prefix_check;

   if (lpTempFileName == NULL || lpPathName == NULL || lpPathName[0] == '\0') {
      SetLastError(ERROR_INVALID_PARAMETER); return 0;
   }
   /* The prefix is a filename component, not another path. On POSIX a slash
    * in the first three characters would otherwise escape the requested temp
    * directory (e.g. prefix "../"). Reject path separators and Win32-invalid
    * filename characters rather than allowing platform-dependent traversal. */
   prefix_check = prefix_len < 3 ? prefix_len : 3;
   for (size_t i = 0; i < prefix_check; ++i) {
      unsigned char c = (unsigned char)prefix[i];
      if (c < 0x20u || strchr("<>:\"/\\|?*", (int)c) != NULL) {
         SetLastError(ERROR_INVALID_PARAMETER);
         return 0;
      }
   }
   if (winapi_resolve_path(lpPathName, &converted, &directory) != 0) {
      winapi_set_error_from_errno(errno); return 0;
   }
   if (stat(directory, &st) != 0) {
      int saved = errno;
      free(converted); winapi_set_error_from_errno(saved); return 0;
   }
   if (!S_ISDIR(st.st_mode)) {
      free(converted); SetLastError(ERROR_PATH_NOT_FOUND); return 0;
   }
   dir_len = strlen(directory);
   if (prefix_len > 3) prefix_len = 3;
   if (dir_len + prefix_len + 10 >= sizeof(path)) {
      free(converted); SetLastError(ERROR_BUFFER_OVERFLOW); return 0;
   }
   first = uUnique != 0 ? (uUnique & 0xffffu) :
           (unsigned int)(((unsigned long)getpid() ^ (unsigned long)time(NULL)) & 0xffffu);
   if (first == 0) first = 1;
   for (tries = 0; tries < (uUnique != 0 ? 1u : 65535u); ++tries) {
      unsigned int value = uUnique != 0 ? first : ((first + tries - 1u) % 65535u) + 1u;
      int written = snprintf(path, sizeof(path), "%s%s%.*s%04X.tmp", directory,
                             (dir_len > 0 && directory[dir_len - 1] == '/') ? "" : "/",
                             (int)prefix_len, prefix, value);
      if (written < 0 || (size_t)written >= sizeof(path)) {
         free(converted); SetLastError(ERROR_BUFFER_OVERFLOW); return 0;
      }
      if (uUnique != 0) {
         memcpy(lpTempFileName, path, (size_t)written + 1);
         free(converted); SetLastError(ERROR_SUCCESS); return value;
      }
      {
         int fd = open(path, O_CREAT | O_EXCL | O_WRONLY
#ifdef O_CLOEXEC
                       | O_CLOEXEC
#endif
                       , 0600);
         if (fd >= 0) {
            if (close(fd) != 0) {
               int saved = errno; (void)unlink(path); free(converted);
               winapi_set_error_from_errno(saved); return 0;
            }
            memcpy(lpTempFileName, path, (size_t)written + 1);
            free(converted); SetLastError(ERROR_SUCCESS); return value;
         }
         if (errno != EEXIST) {
            int saved = errno; free(converted); winapi_set_error_from_errno(saved); return 0;
         }
      }
   }
   free(converted); SetLastError(ERROR_FILE_EXISTS); return 0;
}

HANDLE WINAPI FindFirstFile(LPCTSTR lpFileName, LPWIN32_FIND_DATA lpFindFileData)
{
   filesearch *search;
   filesearchs *entry;
   int rc;
   char *converted_path = NULL;
   const char *pattern;
   int failure = 0;

   if (!winapi_inited() || lpFileName == NULL || lpFindFileData == NULL) {
      errno = EINVAL;
      SetLastError(ERROR_INVALID_PARAMETER);
      return INVALID_HANDLE_VALUE;
   }
   pattern = lpFileName;
   if (is_win_path(lpFileName)) {
      converted_path = win2posix_path(lpFileName);
      if (converted_path == NULL) {
         failure = errno ? errno : ENOMEM;
         set_find_last_error_from_errno(failure);
         errno = failure;
         return INVALID_HANDLE_VALUE;
      }
      pattern = converted_path;
   }
   search = (filesearch *)calloc(1, sizeof(*search));
   if (search == NULL) {
      free(converted_path);
      errno = ENOMEM;
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return INVALID_HANDLE_VALUE;
   }

   {
      int glob_flags = GLOB_MARK;
#ifdef GLOB_PERIOD
      /* Win32 wildcard enumeration includes hidden dot-names; plain POSIX glob
       * silently omits them, producing incomplete FindFirst/FindNext results. */
      glob_flags |= GLOB_PERIOD;
#endif
      rc = glob(pattern, glob_flags, NULL, &search->results);
   }
   free(converted_path);
   if (rc != 0 || search->results.gl_pathc == 0) {
      if (rc == GLOB_NOSPACE) failure = ENOMEM;
      else if (rc == GLOB_NOMATCH || search->results.gl_pathc == 0) failure = ENOENT;
      else failure = EIO;
      globfree(&search->results);
      free(search);
      errno = failure;
      set_find_last_error_from_errno(failure);
      return INVALID_HANDLE_VALUE;
   }
   if (!copy_find_filename(lpFindFileData, search->results.gl_pathv[0])) {
      int saved = errno ? errno : ENAMETOOLONG;
      globfree(&search->results);
      free(search);
      errno = saved;
      set_find_last_error_from_errno(saved);
      return INVALID_HANDLE_VALUE;
   }

   search->idx = 1; /* FindFirstFile ha già restituito il primo risultato. */
   entry = (filesearchs *)calloc(1, sizeof(*entry));
   if (entry == NULL) {
      globfree(&search->results);
      free(search);
      errno = ENOMEM;
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return INVALID_HANDLE_VALUE;
   }
   entry->handle = winapi_allocate_handle_token();
   if (entry->handle == NULL) {
      globfree(&search->results);
      free(search);
      free(entry);
      errno = EMFILE;
      SetLastError(ERROR_TOO_MANY_OPEN_FILES);
      return INVALID_HANDLE_VALUE;
   }
   entry->pFileSearch = search;
   pthread_mutex_lock(&g_win_find_handles_lock);
   TAILQ_INSERT_TAIL(filesearchs_head, entry, filesearchs_entries);
   pthread_mutex_unlock(&g_win_find_handles_lock);
   return entry->handle;
}

BOOL WINAPI FindNextFile(HANDLE hFindFile, LPWIN32_FIND_DATA lpFindFileData)
{
   filesearchs *entry;
   BOOL result = FALSE;
   DWORD error = ERROR_INVALID_HANDLE;
   if (hFindFile == INVALID_HANDLE_VALUE || hFindFile == NULL || lpFindFileData == NULL) {
      errno = EINVAL;
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   pthread_mutex_lock(&g_win_find_handles_lock);
   TAILQ_FOREACH(entry, filesearchs_head, filesearchs_entries) {
      if (entry->handle == hFindFile) {
         filesearch *search = entry->pFileSearch;
         if (search->idx >= search->results.gl_pathc) {
            errno = ENOENT;
            error = ERROR_NO_MORE_FILES;
         } else if (!copy_find_filename(lpFindFileData, search->results.gl_pathv[search->idx])) {
            int saved = errno;
            if (saved == EOVERFLOW) error = ERROR_INVALID_DATA;
            else if (saved == ENAMETOOLONG) error = ERROR_FILENAME_EXCED_RANGE;
            else if (saved == ENOENT) error = ERROR_FILE_NOT_FOUND;
            else if (saved == EACCES || saved == EPERM) error = ERROR_ACCESS_DENIED;
            else error = ERROR_GEN_FAILURE;
            errno = saved;
         } else {
            search->idx++;
            result = TRUE;
         }
         break;
      }
   }
   if (!result && entry == NULL) {
      errno = EBADF;
      error = ERROR_INVALID_HANDLE;
   }
   pthread_mutex_unlock(&g_win_find_handles_lock);
   if (!result) SetLastError(error);
   return result;
}

BOOL WINAPI FindClose(HANDLE hFindFile)
{
   filesearchs *entry;
   if (hFindFile == INVALID_HANDLE_VALUE || hFindFile == NULL) {
      errno = EINVAL;
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
   }
   pthread_mutex_lock(&g_win_find_handles_lock);
   TAILQ_FOREACH(entry, filesearchs_head, filesearchs_entries) {
      if (entry->handle == hFindFile) {
         filesearch *search = entry->pFileSearch;
         TAILQ_REMOVE(filesearchs_head, entry, filesearchs_entries);
         globfree(&search->results);
         free(search);
         free(entry);
         pthread_mutex_unlock(&g_win_find_handles_lock);
         return TRUE;
      }
   }
   pthread_mutex_unlock(&g_win_find_handles_lock);
   errno = EBADF;
   SetLastError(ERROR_INVALID_HANDLE);
   return FALSE;
}

DWORD WINAPI GetLogicalDriveStrings(DWORD nBufferLength, LPTSTR lpBuffer)
{
   struct disks *entry;
   struct _disksHead head;
   size_t required = 1; /* final NUL */
   size_t offset = 0;

   TAILQ_INIT(&head);
   if (update_drive_list(&head, 0) < 0) {
      clear_drive_list(&head);
      if (lpBuffer != NULL && nBufferLength > 0) lpBuffer[0] = '\0';
      return 0;
   }
   TAILQ_FOREACH(entry, &head, disks_entries) {
      if (entry->pDisk != NULL) {
         size_t drive_len = strlen(entry->pDisk->drive);
         if (drive_len > SIZE_MAX - required - 2u || required + drive_len + 2u > UINT32_MAX) {
            clear_drive_list(&head);
            SetLastError(ERROR_ARITHMETIC_OVERFLOW);
            return 0;
         }
         /* GetLogicalDriveStrings returns root paths (for example C:\), not
          * bare drive designators (C:). Include each entry NUL plus final NUL. */
         required += drive_len + 2u;
      }
   }
   if (lpBuffer == NULL || nBufferLength == 0 || (size_t)nBufferLength < required) {
      clear_drive_list(&head);
      return (DWORD)required;
   }
   TAILQ_FOREACH(entry, &head, disks_entries) {
      size_t drive_len;
      if (entry->pDisk == NULL) continue;
      drive_len = strlen(entry->pDisk->drive);
      memcpy(lpBuffer + offset, entry->pDisk->drive, drive_len);
      offset += drive_len;
      lpBuffer[offset++] = '\\';
      lpBuffer[offset++] = '\0';
   }
   lpBuffer[offset] = '\0';
   clear_drive_list(&head);
   return (DWORD)(required - 1);
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

/* List USB storage devices using libudev.
 *
 * gcc -o udev_list_usb_storage udev_list_usb_storage.c -ludev
 * ./udev_list_usb_storage
 */
#if(ENABLE_UDEV == 1)
#include <libudev.h>
#include <stdio.h>

static struct udev_device*
get_child(struct udev* udev, struct udev_device* parent, const char* subsystem)
{
   struct udev_device* child = NULL;
   struct udev_enumerate *enumerate = udev_enumerate_new(udev);

   udev_enumerate_add_match_parent(enumerate, parent);
   udev_enumerate_add_match_subsystem(enumerate, subsystem);
   udev_enumerate_scan_devices(enumerate);

   struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
   struct udev_list_entry *entry;

   udev_list_entry_foreach(entry, devices) {
      const char *path = udev_list_entry_get_name(entry);
      child = udev_device_new_from_syspath(udev, path);
      break;
   }

   udev_enumerate_unref(enumerate);
   return child;
}

static int is_usb_mass_storage(struct udev* udev, const char* device)
{
   int rc = 0;
   struct udev_enumerate* enumerate = udev_enumerate_new(udev);

   udev_enumerate_add_match_subsystem(enumerate, "scsi");
   udev_enumerate_add_match_property(enumerate, "DEVTYPE", "scsi_device");
   udev_enumerate_scan_devices(enumerate);

   struct udev_list_entry *devices = udev_enumerate_get_list_entry(enumerate);
   struct udev_list_entry *entry      = NULL;
   struct udev_device     *scsi       = NULL;
   struct udev_device     *block      = NULL;
   struct udev_device     *scsi_disk  = NULL;
   struct udev_device     *usb        = NULL;

   udev_list_entry_foreach(entry, devices) 
   {
      const char* path = udev_list_entry_get_name(entry);
      scsi             = udev_device_new_from_syspath(udev, path);
      block            = get_child(udev, scsi, "block");
      scsi_disk        = get_child(udev, scsi, "scsi_disk");
      usb              = udev_device_get_parent_with_subsystem_devtype(scsi, "usb", "usb_device");

      if( block && scsi_disk && usb ) 
      {
         const char *devnode = udev_device_get_devnode(block);
         if( strncmp(devnode, device, strlen(devnode)) == 0 ) 
         {
            // printf("block = %s, usb = %s:%s, scsi = %s\n",
            //   udev_device_get_devnode(block),
            //   udev_device_get_sysattr_value(usb, "idVendor"),
            //   udev_device_get_sysattr_value(usb, "idProduct"),
            //   udev_device_get_sysattr_value(scsi, "vendor"));
            rc = 1; break;
         }
      }

      if( block )     udev_device_unref(block);
      if( scsi_disk ) udev_device_unref(scsi_disk);
      udev_device_unref(scsi);
   }

   udev_enumerate_unref(enumerate);
   return rc;
}
#else
/* Senza libudev non distinguiamo i dischi USB: tutto cio' che e' montato e' DRIVE_FIXED. */
struct udev;
static struct udev *udev_new(void) { return NULL; }
static void udev_unref(struct udev *u) { (void)u; }
static int is_usb_mass_storage(struct udev *u, const char *device) { (void)u; (void)device; return 0; }
#endif

UINT WINAPI GetDriveType(
  _In_opt_ LPCTSTR lpRootPathName
)
{
   if (lpRootPathName == NULL) return DRIVE_UNKNOWN;
   
   UINT rc = DRIVE_UNKNOWN;
   
   struct udev* udev = udev_new();

   struct disks *disk_entry = NULL;
   struct _disksHead disksHead;
   struct _disksHead *disks_head = &disksHead;
   
   TAILQ_INIT(disks_head);
   update_drive_list(disks_head, 0);
   TAILQ_FOREACH(disk_entry, disks_head, disks_entries) 
   {
      disk *pDisk = disk_entry->pDisk;
      if(pDisk)
      {
         if (strncmp(lpRootPathName, pDisk->drive, strlen(pDisk->drive)) == 0)
         {
            if( is_usb_mass_storage(udev, pDisk->device) ) rc = DRIVE_REMOVABLE;
            else                                           rc = DRIVE_FIXED;
            break;
         }
      }
   }

   clear_drive_list(disks_head);
   udev_unref(udev);

   return rc;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////////////

BOOL WINAPI GetDiskFreeSpaceEx(
  _In_opt_  LPCTSTR         lpDirectoryName,
  _Out_opt_ PULARGE_INTEGER lpFreeBytesAvailable,
  _Out_opt_ PULARGE_INTEGER lpTotalNumberOfBytes,
  _Out_opt_ PULARGE_INTEGER lpTotalNumberOfFreeBytes
)
{
   if (lpDirectoryName == NULL) lpDirectoryName = "."; /* directory corrente */
   struct statvfs stat;
   char *converted_path = NULL;
   const char *posix_path = lpDirectoryName;
   uint64_t available, total, free_bytes;

   if (is_win_path(lpDirectoryName)) {
      converted_path = win2posix_path(lpDirectoryName);
      if (converted_path == NULL) {
         int saved = errno ? errno : ENOMEM;
         SetLastError(winapi_error_from_errno(saved));
         return FALSE;
      }
      posix_path = converted_path;
   }
   if (statvfs(posix_path, &stat) != 0) {
      int saved = errno;
      free(converted_path);
      SetLastError(winapi_error_from_errno(saved));
      return FALSE;
   }
   /* Do not publish partially computed output if the host's counters cannot
    * be represented in ULONGLONG. */
   /* f_frsize is the allocation fragment size; f_bsize is only the preferred
    * transfer block size on filesystems where the two differ. */
   {
      uint64_t allocation_unit = stat.f_frsize != 0 ? (uint64_t)stat.f_frsize :
                                                       (uint64_t)stat.f_bsize;
      if (!checked_u64_multiply(allocation_unit, (uint64_t)stat.f_bavail, &available) ||
          !checked_u64_multiply(allocation_unit, (uint64_t)stat.f_blocks, &total) ||
          !checked_u64_multiply(allocation_unit, (uint64_t)stat.f_bfree, &free_bytes)) {
         free(converted_path);
         SetLastError(ERROR_ARITHMETIC_OVERFLOW);
         return FALSE;
      }
   }
   free(converted_path);
   if (lpFreeBytesAvailable != NULL) lpFreeBytesAvailable->QuadPart = available;
   if (lpTotalNumberOfBytes != NULL) lpTotalNumberOfBytes->QuadPart = total;
   if (lpTotalNumberOfFreeBytes != NULL) lpTotalNumberOfFreeBytes->QuadPart = free_bytes;
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

BOOL WINAPI GetDiskFreeSpace(LPCTSTR lpRootPathName, LPDWORD lpSectorsPerCluster,
   LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters,
   LPDWORD lpTotalNumberOfClusters)
{
   char *converted = NULL;
   const char *path = lpRootPathName;
   struct statvfs sv;
   uint64_t fragment, cluster, total_bytes, free_bytes;
   DWORD sectors, total_clusters, free_clusters;
   if (lpSectorsPerCluster == NULL || lpBytesPerSector == NULL ||
       lpNumberOfFreeClusters == NULL || lpTotalNumberOfClusters == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER); return FALSE;
   }
   if (path == NULL || path[0] == '\0') path = ".";
   if (is_win_path(path)) {
      converted = win2posix_path(path);
      if (converted == NULL) { winapi_set_error_from_errno(errno ? errno : ENOMEM); return FALSE; }
      path = converted;
   }
   if (statvfs(path, &sv) != 0) {
      int saved = errno; free(converted); winapi_set_error_from_errno(saved); return FALSE;
   }
   fragment = sv.f_frsize != 0 ? (uint64_t)sv.f_frsize : (uint64_t)sv.f_bsize;
   if (fragment == 0 || fragment > UINT32_MAX ||
       !checked_u64_multiply((uint64_t)sv.f_blocks, fragment, &total_bytes) ||
       !checked_u64_multiply((uint64_t)sv.f_bfree, fragment, &free_bytes)) {
      free(converted); SetLastError(ERROR_ARITHMETIC_OVERFLOW); return FALSE;
   }
   /* Expose a conventional 512-byte logical sector and choose a cluster size
    * at least as large as the filesystem fragment, rounded up to whole sectors. */
   sectors = (DWORD)(fragment / 512u + (fragment % 512u != 0u));
   if (sectors == 0 || sectors > UINT32_MAX / 512u) {
      free(converted); SetLastError(ERROR_ARITHMETIC_OVERFLOW); return FALSE;
   }
   cluster = (uint64_t)sectors * 512u;
   total_clusters = (DWORD)(total_bytes / cluster > UINT32_MAX ? UINT32_MAX : total_bytes / cluster);
   free_clusters = (DWORD)(free_bytes / cluster > UINT32_MAX ? UINT32_MAX : free_bytes / cluster);
   free(converted);
   *lpSectorsPerCluster = sectors; *lpBytesPerSector = 512u;
   *lpNumberOfFreeClusters = free_clusters; *lpTotalNumberOfClusters = total_clusters;
   SetLastError(ERROR_SUCCESS); return TRUE;
}

#define DOTDOT_HANDLE (-2)

// Abstract:   Make a path out of its parts
// Parameters: Path: Object to be made
//             Drive: Logical drive , only for compatibility , not considered
//             Directory: Directory part of path
//             Filename: File part of path
//             Extension: Extension part of path (includes the leading point)
// Returns:    Path is changed
// Comment:    Note that the concept of an extension is not available in Linux,
//             nevertheless it is considered

void win_makepath(char* Path, const char* Drive, const char* Directory, const char* File, const char* Extension);

void _makepath(char* Path, const char* Drive, const char* Directory, const char* File, const char* Extension)
{
   if (Path == NULL) { errno = EINVAL; return; }
   win_makepath(Path, Drive, Directory, File, Extension);

   char* posix_path = Path;
   if(is_win_path(Path)) posix_path = win2posix_path(Path);
   if( posix_path && posix_path != Path )
   {
      strcpy(Path, posix_path);
      free(posix_path);
   }
}

void win_makepath(char* Path, const char* Drive, const char* Directory, const char* File, const char* Extension)
{  
   const char* _Drive     = Drive;
   const char* _Directory = Directory;
   while (Drive != NULL && *Drive != '\0')
   {
      *Path = *Drive;
      Path++;
      Drive++;
   }
   if (_Drive != NULL && *_Drive != '\0')
   {
      if( !(_Directory != NULL && *_Directory == '.') ) // Se il percorso è relativo non aggiungo il separatore C:.\DIRNAME [ok] C:\\.\DIRNAME [no]
      {
         int l = strlen(_Drive);
         if (l > 0 && _Drive[l - 1] != WIN_FILE_SEPARATOR && _Drive[l - 1] != POSIX_FILE_SEPARATOR)
         {
            *Path = WIN_FILE_SEPARATOR;
            Path++;
         }
      }
   }
   while (Directory != NULL && *Directory != '\0')
   {
      *Path = *Directory;
      Path++;
      Directory++;
   }
   if (_Directory != NULL && *_Directory != '\0')
   {
      int l = strlen(_Directory);
      if (l > 0 && _Directory[l - 1] != WIN_FILE_SEPARATOR && _Directory[l - 1] != POSIX_FILE_SEPARATOR)
      {
         *Path = WIN_FILE_SEPARATOR;
         Path++;
      }
   }
   while (File != NULL && *File != '\0')
   {
      *Path = *File;
      Path++;
      File++;
   }
   if (Extension != NULL && *Extension != '\0')
   {
      if (strlen(Extension) > 0 && Extension[0] != '.')
      {
         *Path = '.';
         Path++;
      }
   }
   while (Extension != NULL && *Extension != '\0')
   {
      *Path = *Extension;
      Path++;
      Extension++;
   }
   *Path = '\0';
   return;
}

void win_splitpath(const char* path, char* drv, char* dir, char* name, char* ext);

void _splitpath(const char* path, char* drv, char* dir, char* name, char* ext)
{ 
   if (path == NULL) {
      errno = EINVAL;
      if (drv) *drv = '\0';
      if (dir) *dir = '\0';
      if (name) *name = '\0';
      if (ext) *ext = '\0';
      return;
   }
   /*
   char* posix_path = (char *)path;
   if( is_win_path(path) ) posix_path = win2posix_path(path);
   if( posix_path )
   {
      posix_splitpath(posix_path, drv, dir, name, ext);
      if( posix_path != path ) free(posix_path);
   }
   */
   /* posix2win_path always returns owned storage. It can legitimately fail
    * for an absolute POSIX path before the drive map is initialized; parsing
    * must still work in that case, so fall back to separator normalization. */
   char* win_path = posix2win_path(path);
   if (win_path == NULL) {
      win_path = strdup(path);
      if (win_path != NULL) {
         size_t i;
         for (i = 0; win_path[i] != '\0'; ++i)
            if (win_path[i] == '/') win_path[i] = '\\';
      }
   }
   if (win_path != NULL) {
      win_splitpath(win_path, drv, dir, name, ext);
      free(win_path);
   } else {
      errno = ENOMEM;
      if (drv) *drv = '\0';
      if (dir) *dir = '\0';
      if (name) *name = '\0';
      if (ext) *ext = '\0';
   }
}

void win_splitpath(const char* path, char* drv, char* dir, char* name, char* ext)
{
    const char* end; /* end of processed string */
    const char* p;   /* search pointer */
    const char* s;   /* copy pointer */
    
    /* extract drive name */
    if (path[0] && path[1]==':') {
       if (drv) { drv[0] = path[0]; drv[1] = ':'; drv[2] = '\0'; }
       path += 2; /* Directory/name components exclude the drive prefix. */
    } else if (drv) {
        *drv = '\0';
    }
    
    /* search for end of string or stream separator */
    for( end=path; *end /*&& *end!=':'*/; )
        end++;
    
    /* search for begin of file extension */
   for (p = end; p > path && * --p != WIN_FILE_SEPARATOR && *p != POSIX_FILE_SEPARATOR;)
        if (*p == '.') {
            end = p;
            break;
        }
    
    if (ext)
        for(s=end; (*ext=*s++); )
            ext++;
    
    /* search for end of directory name */
    for(p=end; p>path; )
      if (*--p==WIN_FILE_SEPARATOR || *p==POSIX_FILE_SEPARATOR) {
            p++;
            break;
        }
    
    if (name) {
        for(s=p; s<end; )
            *name++ = *s++;
        
        *name = '\0';
    }
    
    if (dir) {
        for(s=path; s<p; )
            *dir++ = *s++;
        
        *dir = '\0';
    }
}


int _match_spec(const char* spec, const char* text) {
    const char *star = NULL;
    const char *retry = NULL;

    if (spec == NULL || text == NULL)
        return 0;

    /*
     * Iterative wildcard matching avoids unbounded C recursion and the
     * exponential backtracking of the former recursive implementation.
     * `star` records the latest '*' and `retry` the next text position
     * to try for the portion following that star.
     */
    while (*text != '\0') {
        if (*spec == '?' || *spec == *text) {
            ++spec;
            ++text;
        } else if (*spec == '*') {
            star = spec++;
            retry = text;
        } else if (star != NULL) {
            spec = star + 1;
            text = ++retry;
        } else {
            return 0;
        }
    }

    while (*spec == '*')
        ++spec;
    return *spec == '\0';
}

int match_spec(const char* spec, const char* text) {
    if (spec == NULL || text == NULL)
        return 0;

    /* On Windows, *.* matches everything, including extensionless names. */
    if (strcmp(spec, "*.*") == 0)
        return 1;

    return _match_spec(spec, text);
}

typedef struct fhandle_t {
    DIR* dstream;
    short dironly;
    char* spec;
} fhandle_t;


typedef struct fhandles 
{
   TAILQ_ENTRY(fhandles) fhandles_entries;
   struct _finddata_t fileinfo;
} fhandles;

TAILQ_HEAD(_fhandlesHead, fhandles);

static void fill_finddata(struct stat* st, const char* name,
        struct _finddata_t* fileinfo);

static intptr_t findfirst_dotdot(const char* filespec,
        struct _finddata_t* fileinfo);

static intptr_t findfirst_in_directory(const char* dirpath,
        const char* spec, struct _finddata_t* fileinfo);

static void findfirst_set_errno();

intptr_t posix_findfirst(const char* filespec, struct _finddata_t* fileinfo);
int      posix_findnext(intptr_t fhandle, struct _finddata_t* fileinfo);
int      posix_findclose(intptr_t fhandle);


static void clear_fhandle_list(struct _fhandlesHead *fhandles_head);

static intptr_t update_fhandle_list(
                                 struct _fhandlesHead *fhandles_head,
                                 struct fhandles *curr_handle_entry,
                                 intptr_t handle,
                                 struct _finddata_t *fileinfo)
{
   if (handle == -1 || fhandles_head == NULL || curr_handle_entry == NULL ||
       fileinfo == NULL) {
      errno = EINVAL;
      return -1;
   }

   int rc = -1;
   while (rc) {
      /* Dot entries are deliberately omitted from the CRT result list; do not
       * allocate a node for entries that will not be inserted. */
      if (strcmp(fileinfo->name, ".") != 0 && strcmp(fileinfo->name, "..") != 0) {
         struct fhandles *new_entry = calloc(1, sizeof(*new_entry));
         if (new_entry == NULL) {
            clear_fhandle_list(fhandles_head);
            posix_findclose(handle);
            errno = ENOMEM;
            return -1;
         }
         new_entry->fileinfo = *fileinfo;

         struct fhandles *entry;
         int inserted = 0;
         TAILQ_FOREACH(entry, fhandles_head, fhandles_entries) {
            if (strcmp(entry->fileinfo.name, fileinfo->name) > 0) {
               TAILQ_INSERT_BEFORE(entry, new_entry, fhandles_entries);
               inserted = 1;
               break;
            }
         }
         if (!inserted) TAILQ_INSERT_TAIL(fhandles_head, new_entry, fhandles_entries);
      }
      rc = (posix_findnext(handle, fileinfo) == 0);
   }
   posix_findclose(handle);

   struct fhandles *first = TAILQ_FIRST(fhandles_head);
   if (first != NULL) {
      *curr_handle_entry = *first;
      *fileinfo = first->fileinfo;
      return handle;
   }
   return -1;
}

int search_fhandle_list(struct _fhandlesHead  *fhandles_head, fhandles *curr_handle_entry, struct _finddata_t* fileinfo)
{
   struct fhandles *handle_entry = NULL, *next_handle_entry = NULL;

   TAILQ_FOREACH(handle_entry, fhandles_head, fhandles_entries)
   {
      if (strcmp(handle_entry->fileinfo.name, curr_handle_entry->fileinfo.name) == 0)
      {
         next_handle_entry = TAILQ_NEXT(handle_entry, fhandles_entries);
         if (next_handle_entry)
         {
            *curr_handle_entry = *next_handle_entry;
            *fileinfo = next_handle_entry->fileinfo;
            // printf("_findnext: %s\n", next_handle_entry->fileinfo.name);
            return 0;
         }
      }
   }

   return -1;
}

static void clear_fhandle_list(struct _fhandlesHead  *fhandles_head)
{
   struct fhandles *handle_entry, *prev_handle_entry = NULL;

   TAILQ_FOREACH_REVERSE(handle_entry, fhandles_head, _fhandlesHead, fhandles_entries) 
   {
      if (prev_handle_entry) free(prev_handle_entry);    
      TAILQ_REMOVE(fhandles_head, handle_entry, fhandles_entries);
      prev_handle_entry = handle_entry;
   }
   
   if(prev_handle_entry) free(prev_handle_entry); 
}

typedef struct fsearch_t
{
   struct _fhandlesHead fhandlesHead;
   intptr_t fhandle;
   struct   fhandles curr_handle_entry;
} fsearch_t;

const struct fhandles fhandle_null = { 0 };

typedef struct fsearchs 
{
   TAILQ_ENTRY(fsearchs) fsearchs_entries;
   fsearch_t   fsearch;
} fsearchs;

TAILQ_HEAD(_fsearchsHead, fsearchs);
/*static*/ struct _fsearchsHead fsearchsHead;
/*static*/ struct _fsearchsHead *fsearchs_head = &fsearchsHead;

void   fsearchs_init(void);
struct fsearch_t *fsearch_open(intptr_t handle);
struct fsearch_t *fsearch_get(intptr_t handle);
void   fsearch_close(intptr_t handle);

void fsearchs_init( void )
{
   TAILQ_INIT(fsearchs_head);
}

struct fsearch_t *fsearch_open(intptr_t handle)
{
   struct fsearch_t *pFSearch = NULL;
   
   pFSearch = fsearch_get(handle);
   if( pFSearch == NULL )
   {
      struct fsearchs *new_search_entry = NULL;
      new_search_entry = (fsearchs *)calloc(1, sizeof(fsearchs));
      if (new_search_entry == NULL) {
         errno = ENOMEM;
         return NULL;
      }
      new_search_entry->fsearch.fhandle  = handle; 
      new_search_entry->fsearch.curr_handle_entry = fhandle_null;
      TAILQ_INIT(&new_search_entry->fsearch.fhandlesHead);
      TAILQ_INSERT_TAIL(fsearchs_head, new_search_entry, fsearchs_entries);
      pFSearch = &new_search_entry->fsearch;   
   }
   else
   {
      clear_fhandle_list(&pFSearch->fhandlesHead); 
   }
   return pFSearch;
}

struct fsearch_t *fsearch_get( intptr_t handle )
{
   
   struct fsearch_t *pFSearch = NULL;
   struct fsearchs *search_entry = NULL;
/*
   for (search_entry = TAILQ_FIRST(fsearchs_head); search_entry != NULL; search_entry = tmp_search_entry)
   {
      tmp_search_entry = TAILQ_NEXT(search_entry, fsearchs_entries);
      if (search_entry->fsearch.fhandle == handle)
      {             
         pFSearch = &search_entry->fsearch;
         break;
      }
   }
*/

   TAILQ_FOREACH(search_entry, fsearchs_head, fsearchs_entries) 
   {
      if (search_entry->fsearch.fhandle == handle)
      {             
         pFSearch = &search_entry->fsearch;
         return pFSearch;
      } 
   }

   TAILQ_FOREACH_REVERSE(search_entry, fsearchs_head, _fsearchsHead, fsearchs_entries) 
   {
      if (search_entry->fsearch.fhandle == handle)
      {             
         pFSearch = &search_entry->fsearch;
         return pFSearch;
      } 
   }

   return pFSearch;
}

void fsearch_close( intptr_t handle )
{
   struct fsearchs *search_entry, *tmp_search_entry = NULL;

   for (search_entry = TAILQ_FIRST(fsearchs_head); search_entry != NULL; search_entry = tmp_search_entry)
   {
      tmp_search_entry = TAILQ_NEXT(search_entry, fsearchs_entries);
      if (search_entry->fsearch.fhandle == handle)
      {             
         clear_fhandle_list(&search_entry->fsearch.fhandlesHead);   
         search_entry->fsearch.curr_handle_entry = fhandle_null; 
         TAILQ_REMOVE(fsearchs_head, search_entry, fsearchs_entries); // Remove the item from the tail queue.                                  
         if (search_entry) free(search_entry);  // Free the item as we dont need it anymore.
         break;
      }
   }

}

intptr_t _findfirst(const char* filespec, struct _finddata_t* fileinfo)
{
   intptr_t ret = -1;

   if (filespec == NULL || fileinfo == NULL) {
      errno = EINVAL;
      return -1;
   }

   char *converted_filespec = NULL;
   const char *posix_filespec = filespec;
   intptr_t handle = INVALID_HANDLE;

   if (is_win_path(filespec)) {
      converted_filespec = win2posix_path(filespec);
      if (converted_filespec == NULL) return -1;
      posix_filespec = converted_filespec;
   }
   handle = posix_findfirst(posix_filespec, fileinfo);
   free(converted_filespec);
   if( handle != INVALID_HANDLE )
   {
      // Completo La Ricerca E Salvo I Risultati In Una Lista Ordinata
      struct fsearch_t *pFSearch = fsearch_open(handle);
      if (pFSearch) {
         ret = update_fhandle_list(&pFSearch->fhandlesHead,
                                   &pFSearch->curr_handle_entry, handle, fileinfo);
         if (ret == -1) fsearch_close(handle);
      } else {
         /* fsearch_open failed before taking ownership of the POSIX search. */
         posix_findclose(handle);
      }
   }

   return ret;
}

int _findnext(intptr_t fhandle, struct _finddata_t* fileinfo)
{
   // Eseguo La Ricerca Sulla Lista Ordinata Già Preparata da _findfirst
   struct fsearch_t *pFSearch = fsearch_get(fhandle);
   if (pFSearch)
      return search_fhandle_list(&pFSearch->fhandlesHead, &pFSearch->curr_handle_entry, fileinfo);
   else
      return -1;
}

int  _findclose(intptr_t fhandle)
{
   // Vuoto La Lista Ordinata E Libero Le Risorse
   fsearch_close(fhandle);
   return 0;
}

intptr_t posix_findfirst(const char* filespec, struct _finddata_t* fileinfo) 
{

    const char* rmslash;      /* Rightmost forward slash in filespec. */
    const char* spec;   /* Specification string. */

    if (!fileinfo || !filespec) {
        errno = EINVAL;
        return INVALID_HANDLE;
    }

    if (filespec[0] == '\0') {
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    rmslash = strrchr(filespec, '/');

    if (rmslash != NULL) {
        /*
         * At least one forward slash was found in the filespec
         * string, and rmslash points to the rightmost one. The
         * specification part, if any, begins right after it.
         */
        spec = rmslash + 1;
    } else {
        /*
         * Since no slash were found in the filespec string, its
         * entire content can be used as our spec string.
         */
        spec = filespec;
    }

    if (strcmp(spec, ".") == 0 || strcmp(spec, "..") == 0) {
        /* On Windows, . and .. must return canonicalized names. */
        return findfirst_dotdot(filespec, fileinfo);
    } else if (rmslash == filespec) {
        /*
         * Since the rightmost slash is the first character, we're
         * looking for something located at the file system's root.
         */
        return findfirst_in_directory("/", spec, fileinfo);
    } else if (rmslash != NULL) {
        /*
         * Since the rightmost slash isn't the first one, we're
         * looking for something located in a specific folder. In
         * order to open this folder, we split the folder path from
         * the specification part by overwriting the rightmost
         * forward slash.
         */
        size_t pathlen = strlen(filespec) +1;
        char* dirpath = (char *)alloca(pathlen);
        memcpy(dirpath, filespec, pathlen);
        dirpath[rmslash - filespec] = '\0';
        return findfirst_in_directory(dirpath, spec, fileinfo);
    } else {
        /*
         * Since the filespec doesn't contain any forward slash,
         * we're looking for something located in the current
         * directory.
         */
        return findfirst_in_directory(".", spec, fileinfo);
    }
}

/* Perfom a scan in the directory identified by dirpath. */
static intptr_t findfirst_in_directory(const char* dirpath, const char* spec, struct _finddata_t* fileinfo) 
{

    DIR *dstream;
    fhandle_t *ffhandle;

    if (spec[0] == '\0') {
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    if ((dstream = opendir(dirpath)) == NULL) {
        findfirst_set_errno();
        return INVALID_HANDLE;
    }

   if ((ffhandle = (fhandle_t *)malloc(sizeof(fhandle_t))) == NULL) {
        closedir(dstream);
        errno = ENOMEM;
        return INVALID_HANDLE;
    }

    /* On Windows, *. returns only directories. */
    ffhandle->dironly = strcmp(spec, "*.") == 0 ? 1 : 0;
    ffhandle->dstream = dstream;
    ffhandle->spec = strdup(spec);
    if (ffhandle->spec == NULL) {
        closedir(dstream);
        free(ffhandle);
        errno = ENOMEM;
        return INVALID_HANDLE;
    }

    if (posix_findnext((intptr_t) ffhandle, fileinfo) != 0) {
        /* Keep failure cleanup explicit: this path must release all three
         * resources even when the analyzer cannot follow the intptr_t cast. */
        closedir(ffhandle->dstream);
        free(ffhandle->spec);
        free(ffhandle);
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    return (intptr_t) ffhandle;
}

/* On Windows, . and .. return canonicalized directory names. */
static intptr_t findfirst_dotdot(const char* filespec, struct _finddata_t* fileinfo) 
{

    char* dirname;
    char* canonicalized;
    struct stat st;

    if (stat(filespec, &st) != 0) {
        findfirst_set_errno();
        return INVALID_HANDLE;
    }

    /* Resolve filespec to an absolute path. */
    if ((canonicalized = realpath(filespec, NULL)) == NULL) {
        findfirst_set_errno();
        return INVALID_HANDLE;
    }

    /* Retrieve the basename from it. */
    dirname = basename(canonicalized);

    /* Make sure that we actually have a basename. */
    if (dirname[0] == '\0') {
        free(canonicalized);
        errno = ENOENT;
        return INVALID_HANDLE;
    }

    /* Make sure that we won't overflow finddata_t::name. */
    if (strlen(dirname) > 259) {
        free(canonicalized);
        errno = ENOMEM;
        return INVALID_HANDLE;
    }

    fill_finddata(&st, dirname, fileinfo);

    free(canonicalized);

    /*
     * Return a special handle since we can't return
     * NULL. The findnext and findclose functions know
     * about this custom handle.
     */
    return DOTDOT_HANDLE;
}

/*
 * Windows implementation of _findfirst either returns EINVAL,
 * ENOENT or ENOMEM. This function makes sure that the above
 * implementation doesn't return anything else when an error
 * condition is encountered.
 */
static void findfirst_set_errno() {
    if (errno != ENOENT &&
        errno != ENOMEM &&
        errno != EINVAL) {
        errno = EINVAL;
    }
}

static void fill_finddata(struct stat* st, const char* name,
        struct _finddata_t* fileinfo) {

    fileinfo->attrib = S_ISDIR(st->st_mode) ? _A_SUBDIR : _A_NORMAL;
    fileinfo->size = st->st_size;
    fileinfo->time_create = st->st_ctime;
    fileinfo->time_access = st->st_atime;
    fileinfo->time_write = st->st_mtime;
    strcpy(fileinfo->name, name);
}

int posix_findnext(intptr_t fhandle, struct _finddata_t* fileinfo) 
{

    struct dirent *entry;
    struct fhandle_t* handle;
    struct stat st;

    if (fhandle == DOTDOT_HANDLE) {
        errno = ENOENT;
        return -1;
    }

    if (fhandle == INVALID_HANDLE || !fileinfo) {
        errno = EINVAL;
        return -1;
    }

    handle = (struct fhandle_t*) fhandle;

    for (;;) {
        errno = 0;
        entry = readdir(handle->dstream);
        if (entry == NULL) {
            /* Distinguish end-of-directory from a read error. */
            if (errno == 0)
                errno = ENOENT;
            return -1;
        }

        if (!handle->dironly && !match_spec(handle->spec, entry->d_name))
            continue;

        if (fstatat(dirfd(handle->dstream), entry->d_name, &st, 0) == -1)
            return -1;

        if (handle->dironly && !S_ISDIR(st.st_mode))
            continue;

        fill_finddata(&st, entry->d_name, fileinfo);
        return 0;
    }
}

int posix_findclose(intptr_t fhandle) 
{

    struct fhandle_t* handle;

    if (fhandle == DOTDOT_HANDLE) {
        return 0;
    }

    if (fhandle == INVALID_HANDLE) {
        errno = ENOENT;
        return -1;
    }

    handle = (struct fhandle_t*) fhandle;

    closedir(handle->dstream);
    free(handle->spec);
    free(handle);

    return 0;
}