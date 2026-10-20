
#include <stdlib.h>    // malloc, free
#include <unistd.h>    // chdir, unlink
#include <sys/stat.h>  // mkdir
#include <stdio.h>     // remove
#include <string.h>    // strlen
#include <errno.h>     // errno
#include <fcntl.h>     // fcntl, O_ACCMODE

extern int  is_win_path(const char *path);
extern char *win2posix_path(const char *win_path);
extern char *posix2win_path(const char *posix_path);

int posix_chdir(const char *d) { return chdir(d); }
int win_chdir(const char *d) 
{ 
   int rc = -1; // 0: success, -1: error
   if (d == NULL) { errno = EINVAL; return -1; }
   char *posix_path = (char *)d;
   if( is_win_path(d) ) posix_path = win2posix_path(d);
   if( posix_path )
   {
      if( strlen(posix_path) ) rc = chdir(posix_path);
      else errno = ENOENT;
      if( posix_path != d ) free(posix_path);
   } 
   return rc;
}

int posix_mkdir(const char *d) { return mkdir(d, ACCESSPERMS); }
int win_mkdir(const char *d) 
{ 
   int rc = -1; // 0: success, -1: error
   if (d == NULL) { errno = EINVAL; return -1; }
   char *posix_path = (char *)d;
   if (is_win_path(d)) posix_path = win2posix_path(d);
   if( posix_path )
   {
      if( strlen(posix_path) ) rc = mkdir(posix_path, ACCESSPERMS);
      else errno = ENOENT;
      if (posix_path != d) free(posix_path);
   } 
   return rc;
}

int posix_rmdir(const char *d) { return rmdir(d); }
int win_rmdir(const char *d) 
{
   int rc = -1; // 0: success, -1: error
   if (d == NULL) { errno = EINVAL; return -1; }
   char *posix_path = (char *)d;
   if (is_win_path(d)) posix_path = win2posix_path(d);
   if( posix_path  )
   {
      if( strlen(posix_path) ) rc = rmdir(posix_path);
      else errno = ENOENT;
      if( posix_path != d ) free(posix_path);
   } 
   return rc;
}

int posix_remove(const char *filename) { return remove(filename); }
int win_remove(const char *filename) 
{
   int rc = -1; // 0: success, -1: error
   char *posix_path;
   if (filename == NULL) { errno = EINVAL; return -1; }
   posix_path = (char *)filename;
   if( is_win_path(filename) ) posix_path = win2posix_path(filename);
   if( posix_path )
   {
      /* C remove() removes either a file or an empty directory. unlink() does
       * not, so using it here broke the standard-library compatibility layer. */
      if( strlen(posix_path) ) rc = remove(posix_path);
      else errno = ENOENT;
      if( posix_path != filename ) free(posix_path);
   } 
   return rc;
}

FILE *posix_fopen(const char *filename, const char *mode)  { return fopen(filename, mode); }
FILE *win_fopen(const char *filename, const char *mode) 
{
   FILE * fd = NULL; // fd: success, NULL: error
   if (filename == NULL || mode == NULL) { errno = EINVAL; return NULL; }
   char *posix_path = (char *)filename;
   if (is_win_path(filename)) 
      posix_path = win2posix_path(filename);
   if( posix_path )
   {
      if( strlen(posix_path) ) fd = posix_fopen(posix_path, mode);
      else errno = ENOENT;
      if( posix_path != filename ) free(posix_path);
   } 
   return fd;
}

int posix_fclose(FILE *stream)  { return fclose(stream); }
int win_fclose(FILE *stream)
{
   int fd, flush_rc, sync_rc = 0, close_rc;
   int first_error = 0;
   int descriptor_flags;
   struct stat st;
   if (stream == NULL) { errno = EINVAL; return EOF; }
   fd = fileno(stream);
   flush_rc = fflush(stream);
   if (flush_rc != 0) first_error = errno ? errno : EIO;
   /* Preserve durability for writable regular files, but never silently
    * discard a flush/sync failure. Read-only streams have no pending writes
    * to persist; trying fdatasync on pseudo-files such as /proc/stat can fail
    * even though reading and closing the stream is entirely valid. fclose()
    * must still run to release the stream. */
   descriptor_flags = fd >= 0 ? fcntl(fd, F_GETFL) : -1;
   if (flush_rc == 0 && descriptor_flags >= 0 &&
       (descriptor_flags & O_ACCMODE) != O_RDONLY &&
       fstat(fd, &st) == 0 && S_ISREG(st.st_mode) &&
       fdatasync(fd) != 0) {
      sync_rc = -1;
      first_error = errno ? errno : EIO;
   }
   close_rc = fclose(stream);
   if (close_rc != 0 && first_error == 0) first_error = errno ? errno : EIO;
   if (flush_rc != 0 || sync_rc != 0 || close_rc != 0) {
      errno = first_error ? first_error : EIO;
      return EOF;
   }
   return 0;
}
