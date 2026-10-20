#include <stdint.h>
#include <time.h>

#ifndef __WPRIVATE_H__
#define __WPRIVATE_H__

//---------------------------------------------------------------------------------------------//
typedef struct event 
{
   char name[MAX_PATH]; 
   int  fd;
   unsigned int active_waiters;
	int signaled;
   // sem_t signaled;
   BOOL  autoReset;
} event;

typedef struct events 
{
   TAILQ_ENTRY(events) events_entries;
   HANDLE handle; /* opaque, non-reused token; never dereferenced */
   struct event *pEvent;
} events;

//---------------------------------------------------------------------------------------------//
typedef struct thread 
{
   pthread_t pid;
   DWORD threadId;
   DWORD exitCode;
   BOOL  running;
	DWORD (*start_routine)(LPVOID);
	void  *start_arg;
   unsigned long suspended;
   BOOL finished;
   unsigned int active_waiters;
   int priority;  /* Win32 logical priority; best-effort OS mapping on Linux */
#if defined(__linux__)
   pid_t osThreadId; /* Linux TID used by setpriority; zero until thread starts */
#endif
} thread;

typedef struct threads 
{
   TAILQ_ENTRY(threads) threads_entries;
   HANDLE handle; /* opaque, non-reused token; never dereferenced */
   struct thread *pThread;
} threads;

//---------------------------------------------------------------------------------------------//
typedef struct mutex 
{
   char name[MAX_PATH];  
   pthread_t pid; // pid of the locking thread; state guarded by g_wait_lock
   unsigned long locks;
   BOOL abandoned;
   unsigned int active_waiters;
} mutex;

typedef struct mutexes 
{
   TAILQ_ENTRY(mutexes) mutexes_entries;
   HANDLE handle; /* opaque, non-reused token; never dereferenced */
   mutex *pMutex;
} mutexes;


//---------------------------------------------------------------------------------------------//
typedef struct mapfile 
{
   char  name[MAX_PATH];  
   int   fd;   // file descriptor
   uint64_t fsize;
   DWORD protection;      // PAGE_READONLY or PAGE_READWRITE
   DWORD granted_access;  // normalized FILE_MAP_* rights for this handle
   
} mapfile;

typedef struct mapfiles 
{
   TAILQ_ENTRY(mapfiles) mapfiles_entries;
   mapfile *pMapFile;
} mapfiles;

typedef struct fileview 
{
   void  *shm;
   size_t  shm_size;
   mapfile  *pMapFile;
} fileview;

typedef struct fileviews 
{
   TAILQ_ENTRY(fileviews) fileviews_entries;
   fileview *pFileView;
} fileviews;

//---------------------------------------------------------------------------------------------//
typedef struct filesearch 
{
   glob_t results;
   unsigned int idx; 
} filesearch;

typedef struct filesearchs 
{
   TAILQ_ENTRY(filesearchs) filesearchs_entries;
   HANDLE handle;
   filesearch *pFileSearch;
} filesearchs;

typedef struct disk
{
   char drive[4];
   char device[PATH_MAX];
   char mountpoint[PATH_MAX];
   unsigned int drivetype;
} disk;

typedef struct disks 
{
   TAILQ_ENTRY(disks) disks_entries;
   disk *pDisk;
} disks;

//---------------------------------------------------------------------------------------------//
TAILQ_HEAD(_eventsHead, events);
TAILQ_HEAD(_threadsHead, threads);
TAILQ_HEAD(_mutexesHead, mutexes);
TAILQ_HEAD(_mapFilesHead, mapfiles);
TAILQ_HEAD(_fileViewsHead, fileviews);
TAILQ_HEAD(_fileSearchsHead, filesearchs);
TAILQ_HEAD(_disksHead, disks);
//---------------------------------------------------------------------------------------------//

/* 1 = closed mapping, 0 = not a mapping handle, -1 = close rejected. */
int wshmem_close_handle(HANDLE handle);

HANDLE winapi_allocate_handle_token(void);
int wfile_close_handle(HANDLE handle);
int wfile_begin_io(HANDLE handle, int required_access, int *fd_out, void **lease_out);
void wfile_end_io(void *lease);
int wfile_duplicate_fd(HANDLE handle, int required_access, int *fd_out,
                       int *readable_out, int *writable_out);
int winapi_timespec_to_filetime(const struct timespec *ts, FILETIME *file_time);

BOOL winapi_inited(void);
BOOL handle_already_exists(const char *name);

int  update_drive_list(struct _disksHead *disks_head, char resolve_root);
void clear_drive_list(struct _disksHead *disks_head);
int  resolve_root_path(char *resolvedpath, size_t resolvedpath_capacity);

#endif