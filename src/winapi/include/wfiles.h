
/***********************************************************************************************/
/*                                FILESYSTEM I/O                                               */
/***********************************************************************************************/

#include <unistd.h> // unlink

#ifndef _MAX_PATH
#define _MAX_PATH       260 /* max. length of full pathname */
#endif
#ifndef _MAX_DRIVE
#define _MAX_DRIVE        3 /* max. length of drive component */
#endif
#ifndef _MAX_DIR
#define _MAX_DIR        256 /* max. length of path component */
#endif
#ifndef _MAX_FNAME
#define _MAX_FNAME      256 /* max. length of file name component */
#endif
#ifndef _MAX_EXT
#define _MAX_EXT        256 /* max. length of extension component */
#endif

#ifndef MAX_PATH
#define MAX_PATH        _MAX_PATH
#endif
#ifndef MAX_DRIVE
#define MAX_DRIVE       _MAX_DRIVE
#endif
#ifndef MAX_DIR
#define MAX_DIR         _MAX_DIR
#endif
#ifndef MAX_FNAME
#define MAX_FNAME       _MAX_FNAME
#endif
#ifndef MAX_EXT
#define MAX_EXT         _MAX_EXT
#endif

#ifndef _A_NORMAL
#define _A_NORMAL    (0x00)  // Normal file (no read/write restrictions)
#endif
#ifndef _A_RDONLY
#define _A_RDONLY    (0x01)  // Read only file
#endif
#ifndef _A_HIDDEN
#define _A_HIDDEN    (0x02)  // Hidden file
#endif
#ifndef _A_SYSTEM
#define _A_SYSTEM    (0x04)  // System file
#endif
#ifndef _A_VOLID
#define _A_VOLID     (0x08)  // Volume ID file
#endif
#ifndef _A_SUBDIR
#define _A_SUBDIR    (0x10)  // Subdirectory
#endif
#ifndef _A_ARCH
#define _A_ARCH      (0x20)  // Archive file
#endif

#define FILE_ATTRIBUTE_READONLY          0x00000001  
#define FILE_ATTRIBUTE_HIDDEN            0x00000002  
#define FILE_ATTRIBUTE_SYSTEM            0x00000004  
#define FILE_ATTRIBUTE_DIRECTORY         0x00000010  
#define FILE_ATTRIBUTE_ARCHIVE           0x00000020  
#define FILE_ATTRIBUTE_DEVICE            0x00000040  
#define FILE_ATTRIBUTE_NORMAL            0x00000080  
#define FILE_ATTRIBUTE_TEMPORARY         0x00000100 
#ifndef INVALID_FILE_ATTRIBUTES
#define INVALID_FILE_ATTRIBUTES          0xFFFFFFFFu
#endif

#ifndef FILE_TYPE_UNKNOWN
#define FILE_TYPE_UNKNOWN 0x0000
#define FILE_TYPE_DISK    0x0001
#define FILE_TYPE_CHAR    0x0002
#define FILE_TYPE_PIPE    0x0003
#define FILE_TYPE_REMOTE  0x8000
#endif

#define FIND_FIRST_EX_CASE_SENSITIVE     1
#define FIND_FIRST_EX_LARGE_FETCH        2

#define DRIVE_UNKNOWN     0
#define DRIVE_NO_ROOT_DIR 1
#define DRIVE_REMOVABLE   2
#define DRIVE_FIXED       3
#define DRIVE_REMOTE      4
#define DRIVE_CDROM       5
#define DRIVE_RAMDISK     6

typedef struct _WIN32_FIND_DATA {
  DWORD    dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD    nFileSizeHigh;
  DWORD    nFileSizeLow;
  DWORD    dwReserved0;
  DWORD    dwReserved1;
  TCHAR    cFileName[MAX_PATH];
  TCHAR    cAlternateFileName[14];
} WIN32_FIND_DATA, *PWIN32_FIND_DATA, *LPWIN32_FIND_DATA;

typedef struct _OBJECT_ATTRIBUTES {
  ULONG           Length;
  HANDLE          RootDirectory;
  PUNICODE_STRING ObjectName;
  ULONG           Attributes;
  PVOID           SecurityDescriptor;
  PVOID           SecurityQualityOfService;
}  OBJECT_ATTRIBUTES, *POBJECT_ATTRIBUTES;

/*
// https://github.com/ned14/FastDirectoryEnumerator/blob/master/FastDirectoryEnumerator/FastDirectoryEnumerator.cpp
typedef struct _IO_STATUS_BLOCK {
   union {
      NTSTATUS Status;
      PVOID    Pointer;
   };
   ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;
*/
typedef enum _FINDEX_INFO_LEVELS { 
  FindExInfoStandard,
  FindExInfoBasic,
  FindExInfoMaxInfoLevel
} FINDEX_INFO_LEVELS;

typedef enum _FINDEX_SEARCH_OPS { 
  FindExSearchNameMatch,
  FindExSearchLimitToDirectories,
  FindExSearchLimitToDevices
} FINDEX_SEARCH_OPS;


/* Minimal Win32 file-handle API. Unsupported sharing/security semantics are
 * rejected rather than silently approximated. */
#ifndef GENERIC_READ
#define GENERIC_READ             0x80000000u
#endif
#ifndef GENERIC_WRITE
#define GENERIC_WRITE            0x40000000u
#endif
#ifndef FILE_SHARE_READ
#define FILE_SHARE_READ          0x00000001u
#define FILE_SHARE_WRITE         0x00000002u
#define FILE_SHARE_DELETE        0x00000004u
#endif
#ifndef CREATE_NEW
#define CREATE_NEW               1u
#define CREATE_ALWAYS            2u
#define OPEN_EXISTING            3u
#define OPEN_ALWAYS              4u
#define TRUNCATE_EXISTING        5u
#endif
#ifndef FILE_ATTRIBUTE_NORMAL
#define FILE_ATTRIBUTE_NORMAL    0x00000080u
#endif

BOOL WINAPI ReadFile(
  _In_ HANDLE hFile,
  _Out_ LPVOID lpBuffer,
  _In_ DWORD nNumberOfBytesToRead,
  _Out_opt_ LPDWORD lpNumberOfBytesRead,
  _In_opt_ LPOVERLAPPED lpOverlapped
);

BOOL WINAPI WriteFile(
  _In_ HANDLE hFile,
  _In_ LPCVOID lpBuffer,
  _In_ DWORD nNumberOfBytesToWrite,
  _Out_opt_ LPDWORD lpNumberOfBytesWritten,
  _In_opt_ LPOVERLAPPED lpOverlapped
);

/* Flush buffered file data to the underlying storage device. */
BOOL WINAPI FlushFileBuffers(_In_ HANDLE hFile);

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

BOOL WINAPI SetFilePointerEx(
  _In_ HANDLE hFile,
  _In_ LARGE_INTEGER liDistanceToMove,
  _Out_opt_ PLARGE_INTEGER lpNewFilePointer,
  _In_ DWORD dwMoveMethod
);

DWORD WINAPI SetFilePointer(
  _In_ HANDLE hFile,
  _In_ LONG lDistanceToMove,
  _Inout_ PLONG lpDistanceToMoveHigh,
  _In_ DWORD dwMoveMethod
);

BOOL WINAPI GetFileSizeEx(
  _In_ HANDLE hFile,
  _Out_ PLARGE_INTEGER lpFileSize
);

/* Set the file's end-of-file position to the current file pointer. */
BOOL WINAPI SetEndOfFile(_In_ HANDLE hFile);

BOOL WINAPI GetFileTime(
  _In_ HANDLE hFile,
  _Out_opt_ LPFILETIME lpCreationTime,
  _Out_opt_ LPFILETIME lpLastAccessTime,
  _Out_opt_ LPFILETIME lpLastWriteTime
);

BOOL WINAPI SetFileTime(
  _In_ HANDLE hFile,
  _In_opt_ const FILETIME *lpCreationTime,
  _In_opt_ const FILETIME *lpLastAccessTime,
  _In_opt_ const FILETIME *lpLastWriteTime
);

BOOL WINAPI LockFile(
  _In_ HANDLE hFile,
  _In_ DWORD dwFileOffsetLow,
  _In_ DWORD dwFileOffsetHigh,
  _In_ DWORD nNumberOfBytesToLockLow,
  _In_ DWORD nNumberOfBytesToLockHigh
);

BOOL WINAPI UnlockFile(
  _In_ HANDLE hFile,
  _In_ DWORD dwFileOffsetLow,
  _In_ DWORD dwFileOffsetHigh,
  _In_ DWORD nNumberOfBytesToUnlockLow,
  _In_ DWORD nNumberOfBytesToUnlockHigh
);

DWORD WINAPI GetFileSize(
  _In_ HANDLE hFile,
  _Out_opt_ LPDWORD lpFileSizeHigh
);

DWORD WINAPI GetFileType(_In_ HANDLE hFile);

/* Metadata snapshot for a registered disk-file handle. On POSIX hosts without
 * a portable birth-time field, ftCreationTime is a documented ctime proxy. */
typedef enum _GET_FILEEX_INFO_LEVELS {
  GetFileExInfoStandard = 0,
  GetFileExMaxInfoLevel = 1
} GET_FILEEX_INFO_LEVELS;

typedef struct _WIN32_FILE_ATTRIBUTE_DATA {
  DWORD    dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD    nFileSizeHigh;
  DWORD    nFileSizeLow;
} WIN32_FILE_ATTRIBUTE_DATA, *LPWIN32_FILE_ATTRIBUTE_DATA;

DWORD WINAPI GetFileAttributes(_In_ LPCTSTR lpFileName);
DWORD WINAPI GetTempPath(_In_ DWORD nBufferLength, _Out_ LPTSTR lpBuffer);
UINT WINAPI GetTempFileName(_In_ LPCTSTR lpPathName, _In_ LPCTSTR lpPrefixString, _In_ UINT uUnique, _Out_ LPTSTR lpTempFileName);
BOOL WINAPI SetFileAttributes(_In_ LPCTSTR lpFileName, _In_ DWORD dwFileAttributes);
BOOL WINAPI DeleteFile(_In_ LPCTSTR lpFileName);
BOOL WINAPI CopyFile(_In_ LPCTSTR lpExistingFileName, _In_ LPCTSTR lpNewFileName, _In_ BOOL bFailIfExists);
BOOL WINAPI MoveFile(_In_ LPCTSTR lpExistingFileName, _In_ LPCTSTR lpNewFileName);
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
BOOL WINAPI MoveFileEx(_In_ LPCTSTR lpExistingFileName, _In_ LPCTSTR lpNewFileName, _In_ DWORD dwFlags);
BOOL WINAPI CreateDirectory(_In_ LPCTSTR lpPathName, _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes);
BOOL WINAPI RemoveDirectory(_In_ LPCTSTR lpPathName);
BOOL WINAPI GetFileAttributesEx(
  _In_ LPCTSTR lpFileName,
  _In_ GET_FILEEX_INFO_LEVELS fInfoLevelId,
  _Out_ LPVOID lpFileInformation
);

typedef struct _BY_HANDLE_FILE_INFORMATION {
  DWORD    dwFileAttributes;
  FILETIME ftCreationTime;
  FILETIME ftLastAccessTime;
  FILETIME ftLastWriteTime;
  DWORD    dwVolumeSerialNumber;
  DWORD    nFileSizeHigh;
  DWORD    nFileSizeLow;
  DWORD    nNumberOfLinks;
  DWORD    nFileIndexHigh;
  DWORD    nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION, *PBY_HANDLE_FILE_INFORMATION,
  *LPBY_HANDLE_FILE_INFORMATION;

BOOL WINAPI GetFileInformationByHandle(
  _In_ HANDLE hFile,
  _Out_ LPBY_HANDLE_FILE_INFORMATION lpFileInformation
);

/* Supported GetFileInformationByHandleEx information classes. The backend
 * implements FileBasicInfo, FileStandardInfo, FileAttributeTagInfo, FileIdInfo,
 * FileAlignmentInfo, FileNameInfo and FileNormalizedNameInfo for registered
 * regular-file handles. Name information requires Linux /proc/self/fd. */
typedef enum _FILE_INFO_BY_HANDLE_CLASS {
  FileBasicInfo = 0,
  FileStandardInfo = 1,
  FileNameInfo = 2,
  FileRenameInfo = 3,
  FileDispositionInfo = 4,
  FileAllocationInfo = 5,
  FileEndOfFileInfo = 6,
  FileStreamInfo = 7,
  FileCompressionInfo = 8,
  FileAttributeTagInfo = 9,
  FileIdBothDirectoryInfo = 10,
  FileIdBothDirectoryRestartInfo = 11,
  FileIoPriorityHintInfo = 12,
  FileRemoteProtocolInfo = 13,
  FileFullDirectoryInfo = 14,
  FileFullDirectoryRestartInfo = 15,
  FileStorageInfo = 16,
  FileAlignmentInfo = 17,
  FileIdInfo = 18,
  FileIdExtdDirectoryInfo = 19,
  FileIdExtdDirectoryRestartInfo = 20,
  FileDispositionInfoEx = 21,
  FileRenameInfoEx = 22,
  FileCaseSensitiveInfo = 23,
  FileNormalizedNameInfo = 24,
  MaximumFileInfoByHandleClass = 25
} FILE_INFO_BY_HANDLE_CLASS, *PFILE_INFO_BY_HANDLE_CLASS;

typedef struct _FILE_BASIC_INFO {
  LARGE_INTEGER CreationTime;
  LARGE_INTEGER LastAccessTime;
  LARGE_INTEGER LastWriteTime;
  LARGE_INTEGER ChangeTime;
  DWORD FileAttributes;
} FILE_BASIC_INFO, *PFILE_BASIC_INFO;

typedef struct _FILE_STANDARD_INFO {
  LARGE_INTEGER AllocationSize;
  LARGE_INTEGER EndOfFile;
  DWORD NumberOfLinks;
  BYTE DeletePending;
  BYTE Directory;
} FILE_STANDARD_INFO, *PFILE_STANDARD_INFO;

typedef struct _FILE_ATTRIBUTE_TAG_INFO {
  DWORD FileAttributes;
  DWORD ReparseTag;
} FILE_ATTRIBUTE_TAG_INFO, *PFILE_ATTRIBUTE_TAG_INFO;

typedef struct _FILE_ID_128 {
  BYTE Identifier[16];
} FILE_ID_128, *PFILE_ID_128;

typedef struct _FILE_ID_INFO {
  ULONGLONG VolumeSerialNumber;
  FILE_ID_128 FileId;
} FILE_ID_INFO, *PFILE_ID_INFO;

typedef struct _FILE_ALIGNMENT_INFO {
  ULONG AlignmentRequirement;
} FILE_ALIGNMENT_INFO, *PFILE_ALIGNMENT_INFO;

/* FILE_NAME_INFO uses 16-bit UTF-16 code units regardless of the host C
 * wchar_t width. WORD keeps this structure's Win32 ABI stable on POSIX hosts. */
typedef struct _FILE_NAME_INFO {
  DWORD FileNameLength; /* length in bytes, excluding any terminator */
  WORD FileName[1];
} FILE_NAME_INFO, *PFILE_NAME_INFO;

typedef struct _FILE_END_OF_FILE_INFO {
  LARGE_INTEGER EndOfFile;
} FILE_END_OF_FILE_INFO, *PFILE_END_OF_FILE_INFO;

/* SetFileInformationByHandle currently supports FileBasicInfo (timestamps
 * representable by POSIX) and FileEndOfFileInfo for writable regular files. */
BOOL WINAPI SetFileInformationByHandle(
  _In_ HANDLE hFile,
  _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
  _In_ LPVOID lpFileInformation,
  _In_ DWORD dwBufferSize
);

BOOL WINAPI GetFileInformationByHandleEx(
  _In_ HANDLE hFile,
  _In_ FILE_INFO_BY_HANDLE_CLASS FileInformationClass,
  _Out_ LPVOID lpFileInformation,
  _In_ DWORD dwBufferSize
);

HANDLE WINAPI CreateFile(
  _In_ LPCTSTR lpFileName,
  _In_ DWORD dwDesiredAccess,
  _In_ DWORD dwShareMode,
  _In_opt_ LPSECURITY_ATTRIBUTES lpSecurityAttributes,
  _In_ DWORD dwCreationDisposition,
  _In_ DWORD dwFlagsAndAttributes,
  _In_opt_ HANDLE hTemplateFile
);

DWORD WINAPI GetCurrentDirectory(
  _In_  DWORD  nBufferLength,
  _Out_ LPTSTR lpBuffer
);

BOOL WINAPI SetCurrentDirectory(
  _In_ LPCTSTR lpPathName
);

HANDLE WINAPI FindFirstFile(
  _In_  LPCTSTR           lpFileName,
  _Out_ LPWIN32_FIND_DATA lpFindFileData
);

BOOL WINAPI FindNextFile(
  _In_  HANDLE            hFindFile,
  _Out_ LPWIN32_FIND_DATA lpFindFileData
);

BOOL WINAPI FindClose(
  _Inout_ HANDLE hFindFile
);

BOOL WINAPI GetDiskFreeSpaceEx(
  _In_opt_  LPCTSTR         lpDirectoryName,
  _Out_opt_ PULARGE_INTEGER lpFreeBytesAvailable,
  _Out_opt_ PULARGE_INTEGER lpTotalNumberOfBytes,
  _Out_opt_ PULARGE_INTEGER lpTotalNumberOfFreeBytes
);
BOOL WINAPI GetDiskFreeSpace(
  _In_opt_ LPCTSTR lpRootPathName,
  _Out_ LPDWORD lpSectorsPerCluster,
  _Out_ LPDWORD lpBytesPerSector,
  _Out_ LPDWORD lpNumberOfFreeClusters,
  _Out_ LPDWORD lpTotalNumberOfClusters
);

DWORD WINAPI GetLogicalDriveStrings(
  _In_  DWORD  nBufferLength,
  _Out_ LPTSTR lpBuffer
);

#define GetLogicalDriveStringsA  GetLogicalDriveStrings

UINT WINAPI GetDriveType(
  _In_opt_ LPCTSTR lpRootPathName
);

#define GetDriveTypeA  GetDriveType

#ifndef __WDIR_OPS__
#define __WDIR_OPS__

extern int     posix_chdir(const char *d);
extern int     posix_mkdir(const char *d);
extern int     posix_rmdir(const char *d);
extern int     posix_remove(const char *f);
extern FILE *  posix_fopen(const char *f, const char *m);
extern int     posix_fclose(FILE *stream);

extern int     win_chdir(const char *d);
extern int     win_mkdir(const char *d);
extern int     win_rmdir(const char *d);
extern int     win_remove(const char *f);
extern FILE *  win_fopen(const char *f, const char *m);
extern int     win_fclose(FILE *stream);

#define chdir  win_chdir
#define mkdir  win_mkdir
#define rmdir  win_rmdir
#define remove win_remove
#define fopen  win_fopen
#define fclose win_fclose

#define _chdir(d)   chdir(d)
#define _mkdir(d)   mkdir(d)
#define _rmdir(d)   rmdir(d) 
#define _remove(f)  remove(f)
#define _fopen(f,m) fopen(f,m)
#define _fclose(stream) fclose(stream)

#endif

#ifndef __WFILE_OPS__
#define __WFILE_OPS__
inline long filelength(int fd)
{
   struct stat buf;
   fstat(fd, &buf);
   int size = buf.st_size;
   return size;
}
#define tell(fd)              lseek(fd, 0, SEEK_CUR)
#define chsize(fd, size)  ftruncate (fd, size)
#endif

#ifndef __WPATH_OPS__
#define __WPATH_OPS__
void _splitpath(const char* Path,char* Drive,char* Directory,char* Filename,char* Extension);
void _makepath(char* Path,const char* Drive,const char* Directory, const char* File,const char* Extension);
#endif

// http://mathieuturcotte.ca/textes/findfirst/

/*
 * Matches the regular language accepted by findfirst/findnext. More
 * precisely, * matches zero or more characters and ? matches any
 * characters, but only one. Every other characters match themself.
 * To respect the Windows behavior, *.* matches everything.
 */
int match_spec(const char* spec, const char* text);

// http://en.wikipedia.org/wiki/Attrib 
/*
#define _A_NORMAL   0x00    // Normal file.     
#define _A_RDONLY   0x01    // Read only file.  
#define _A_HIDDEN   0x02    // Hidden file.     
#define _A_SYSTEM   0x04    // System file.     
#define _A_SUBDIR   0x10    // Subdirectory.    
#define _A_ARCH     0x20    // Archive file.    
*/
struct _finddata_t {
    unsigned attrib;
    time_t time_create;
    time_t time_access;
    time_t time_write;
    off_t size;
    char name[MAX_PATH];
};


/*
 * Returns a unique search handle identifying the file or group of
 * files matching the filespec specification, which can be used in
 * a subsequent call to findnext or to findclose. Otherwise, findfirst
 * returns NULL and sets errno to EINVAL if filespec or fileinfo
 * was NULL or if the operating system returned an unexpected error
 * and ENOENT if the file specification could not be matched.
 */
intptr_t _findfirst(const char* filespec, struct _finddata_t* fileinfo);

/*
 * Find the next entry, if any, that matches the filespec argument
 * of a previous call to findfirst, and then alter the fileinfo
 * structure contents accordingly. If successful, returns 0. Otherwise,
 * returns -1 and sets errno to EINVAL if handle or fileinfo was NULL
 * or if the operating system returned an unexpected error and ENOENT
 * if no more matching files could be found.
 */
int _findnext(intptr_t handle, struct _finddata_t* fileinfo);

/*
 * Closes the specified search handle and releases associated
 * resources. If successful, findclose returns 0. Otherwise, it
 * returns -1 and sets errno to ENOENT, indicating that no more
 * matching files could be found.
 */
int _findclose(intptr_t handle);

/*
#define WIN32_FIND_DATA       _finddata_t
#define FindFirstFile                  _findfirst
#define FindNextFile                 _findnext
#define FindClose                     _findclose
*/

int  is_win_path(const char *path);
char *win2posix_path( const char *win_path );
char *posix2win_path( const char *posix_path );