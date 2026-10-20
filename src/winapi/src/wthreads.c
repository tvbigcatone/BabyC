#define _GNU_SOURCE 1
#include "windows.h"
#include "wprivate.h"

#if(ENABLE_WTHREADS == 1)

#include <stdio.h>
#include <pthread.h>
#include <signal.h>
#include <stdlib.h>
#include <errno.h>  // errno_abort
#include <time.h>
#include <stdint.h>
#include <stdatomic.h>

#include <sys/syscall.h> // __NR_memfd_create
#include <sys/mman.h>   // mmap
#include <sys/ipc.h>
#include <sys/shm.h>    // shm_open
#include <linux/memfd.h> // MFD_ALLOW_SEALING
#include <fcntl.h> // open, read, write, close
#include <sys/resource.h>
#include <sys/types.h>
#include <unistd.h>

extern int pthread_timedjoin_np (pthread_t __th, void **__thread_return,
     __const struct timespec *__abstime);
extern int pthread_tryjoin_np(pthread_t __th, void **__thread_return);

#ifndef MAP_VARIABLE
#define MAP_VARIABLE	0
#endif

extern struct _eventsHead  *events_head;  
extern struct _threadsHead *threads_head; 
extern struct _mutexesHead *mutexes_head;

/* One notification channel for all waitable objects. State transitions and
 * wait predicates are serialized by g_wait_lock; condition-variable wakeups
 * replace polling and cannot be lost between predicate check and sleep. */
static pthread_mutex_t g_wait_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_wait_cond;
static pthread_once_t g_wait_once = PTHREAD_ONCE_INIT;
static int g_wait_init_error;
static clockid_t g_wait_clock = CLOCK_REALTIME;
static void wait_channel_init(void) {
   pthread_condattr_t a;
   int rc = pthread_condattr_init(&a);
   if (rc != 0) { g_wait_init_error = rc; return; }
#ifdef CLOCK_MONOTONIC
   /* Use the same clock for deadline creation and timed waits. Falling back to
    * CLOCK_REALTIME is safe only when deadline calculation follows that choice. */
   if (pthread_condattr_setclock(&a, CLOCK_MONOTONIC) == 0)
      g_wait_clock = CLOCK_MONOTONIC;
#endif
   rc = pthread_cond_init(&g_wait_cond, &a);
   (void)pthread_condattr_destroy(&a);
   if (rc != 0) g_wait_init_error = rc;
}
static int wait_channel_ready(void) {
   (void)pthread_once(&g_wait_once, wait_channel_init);
   return g_wait_init_error == 0;
}
static void wait_notify_locked(void) { (void)pthread_cond_broadcast(&g_wait_cond); }

/* Windows thread IDs are DWORDs, unlike pthread_t which is opaque and may be
 * wider than 32 bits. Assign stable process-local IDs instead of truncating it. */
static atomic_uint_fast32_t g_next_thread_id = ATOMIC_VAR_INIT(1);
static _Thread_local DWORD g_thread_id;
static DWORD current_thread_id(void) {
   if (g_thread_id == 0) {
      uint_fast32_t id = atomic_fetch_add_explicit(&g_next_thread_id, 1, memory_order_relaxed);
      if (id == 0 || id > UINT32_MAX) id = atomic_fetch_add_explicit(&g_next_thread_id, 1, memory_order_relaxed);
      g_thread_id = (DWORD)id;
      if (g_thread_id == 0) g_thread_id = 1;
   }
   return g_thread_id;
}

/* Opaque handle tokens are allocated by the shared library-wide allocator. */


/*
struct thread;
struct threads; // threads queue
struct event;
struct events;  // events queue
struct mutex;
struct mutexes; // mutexes queue
*/

// From:
// https://github.com/cloudwu/simplethread/blob/master/simplethread.h

void thread_init_suspend_routine(void)
{
   /* SuspendThread/ResumeThread are explicitly unsupported; do not install
    * process-wide signal handlers with unsafe access to shared registries. */
}

static inline int mutex_init(pthread_mutex_t *mtx)
{
   int rc = 0;
   pthread_mutexattr_t attr;

   pthread_mutexattr_init(&attr);
   pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
   rc = pthread_mutex_init(mtx, &attr);
   pthread_mutexattr_destroy(&attr);
   return rc;
}

static void thread_cleanup_routine(void *hThread)
{
   thread *pThread = (thread *)hThread;
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   pThread->running = FALSE;
#if defined(__linux__)
   pThread->osThreadId = 0; /* never let a stale HANDLE target a recycled Linux TID */
#endif
   pThread->finished = TRUE;
   struct mutexes *me;
   TAILQ_FOREACH(me, mutexes_head, mutexes_entries) {
      mutex *owned = me->pMutex;
      if (owned->locks != 0 && pthread_equal(owned->pid, pThread->pid)) {
         owned->locks = 0; owned->pid = (pthread_t)0; owned->abandoned = TRUE;
      }
   }
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
}

/* Linux exposes a per-thread nice value through setpriority(PRIO_PROCESS,
 * tid, ...). Win32 priorities are only approximated: values above NORMAL
 * request a lower nice value and may be denied without CAP_SYS_NICE; such
 * denial does not invalidate the logical Win32 priority stored by this layer. */
static int win_priority_to_nice(int priority)
{
   switch (priority) {
      case THREAD_PRIORITY_IDLE: return 10;
      case THREAD_PRIORITY_LOWEST: return 5;
      case THREAD_PRIORITY_BELOW_NORMAL: return 2;
      case THREAD_PRIORITY_NORMAL: return 0;
      case THREAD_PRIORITY_ABOVE_NORMAL: return -2;
      case THREAD_PRIORITY_HIGHEST: return -5;
      case THREAD_PRIORITY_TIME_CRITICAL: return -10;
      default: return 0;
   }
}

static void apply_os_thread_priority(thread *pThread)
{
#if defined(__linux__) && defined(SYS_gettid)
   pid_t tid = (pid_t)syscall(SYS_gettid);
   if (tid > 0) {
      pThread->osThreadId = tid;
      (void)setpriority(PRIO_PROCESS, (id_t)tid, win_priority_to_nice(pThread->priority));
   }
#else
   (void)pThread;
#endif
}

static void *thread_start_routine(void *hThread)
{
   thread *pThread = (thread *)hThread;
   g_thread_id = pThread->threadId;
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   pThread->running = TRUE;
   apply_os_thread_priority(pThread);
   pthread_mutex_unlock(&g_wait_lock);
   pthread_cleanup_push(thread_cleanup_routine, hThread);
   pThread->exitCode = pThread->start_routine(pThread->start_arg);
   pthread_cleanup_pop(1);
   return NULL;
}

/***********************************************************************************************/
/*                                    CRITICAL SECTIONS                                        */
/***********************************************************************************************/

inline void WINAPI InitializeCriticalSection(
  _Out_ LPCRITICAL_SECTION lpCriticalSection
)
{
   mutex_init(lpCriticalSection);
}

inline void WINAPI EnterCriticalSection(
  _Inout_ LPCRITICAL_SECTION lpCriticalSection
)
{
 pthread_mutex_lock(lpCriticalSection);
}

inline void WINAPI LeaveCriticalSection(
  _Inout_ LPCRITICAL_SECTION lpCriticalSection
)
{ 
   pthread_mutex_unlock(lpCriticalSection);
}

inline void WINAPI DeleteCriticalSection(
  _Inout_ LPCRITICAL_SECTION lpCriticalSection
)
{
   pthread_mutex_destroy(lpCriticalSection);
}

/***********************************************************************************************/
/*                                             EVENTS                                          */
/***********************************************************************************************/

HANDLE CreateEvent(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
                   BOOL bInitialState, LPCTSTR lpName)
{
   (void)lpEventAttributes;
   if (!winapi_inited()) return NULL;
   if (lpName != NULL && lpName[0] != '\0') {
      SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
      return NULL;
   }
   event *ev = (event *)calloc(1, sizeof(*ev));
   struct events *entry = (struct events *)calloc(1, sizeof(*entry));
   if (ev == NULL || entry == NULL) { free(ev); free(entry); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   if (!wait_channel_ready()) { free(entry); free(ev); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   ev->fd = -1; ev->name[0] = '\0';
   ev->autoReset = !bManualReset; ev->signaled = bInitialState != FALSE;
   entry->handle = winapi_allocate_handle_token();
   if (entry->handle == NULL) { free(entry); free(ev); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   entry->pEvent = ev;
   pthread_mutex_lock(&g_wait_lock);
   TAILQ_INSERT_TAIL(events_head, entry, events_entries);
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
   return entry->handle;
}

HANDLE WINAPI OpenEvent(DWORD dwDesiredAccess, BOOL bInheritHandle, LPCTSTR lpName)
{
   (void)dwDesiredAccess; (void)bInheritHandle; (void)lpName;
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return NULL;
}

BOOL WINAPI SetEvent(
  _In_ HANDLE hEvent
)
{
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   event *pEvent = NULL; struct events *entry;
   TAILQ_FOREACH(entry, events_head, events_entries) if (entry->handle == hEvent) { pEvent=entry->pEvent; break; }
   if (pEvent == NULL) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
   pEvent->signaled = 1;
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
   return TRUE;
}

BOOL WINAPI ResetEvent(
  _In_ HANDLE hEvent
)
{
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   event *pEvent = NULL; struct events *entry;
   TAILQ_FOREACH(entry, events_head, events_entries) if (entry->handle == hEvent) { pEvent=entry->pEvent; break; }
   if (pEvent == NULL) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
   pEvent->signaled = 0;
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
   return TRUE;
}

BOOL WINAPI PulseEvent(HANDLE hEvent)
{
   /* Win32 itself documents PulseEvent as unreliable. A portable equivalent
    * cannot be implemented by setting/resetting around a broadcast: waiters
    * must reacquire the lock after reset and would miss the pulse. Never spin. */
   (void)hEvent;
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return FALSE;
}


/***********************************************************************************************/
/*                                            THREADS                                          */
/***********************************************************************************************/

HANDLE WINAPI CreateThread(
  _In_opt_  LPSECURITY_ATTRIBUTES  lpThreadAttributes,
  _In_      SIZE_T                 dwStackSize,
  _In_      LPTHREAD_START_ROUTINE lpStartAddress,
  _In_opt_  LPVOID                 lpParameter,
  _In_      DWORD                  dwCreationFlags,
  _Out_opt_ LPDWORD                lpThreadId
)
{

   /* Initialize the registry before creating any object. */
   if(!winapi_inited()) { SetLastError(ERROR_GEN_FAILURE); return NULL; }

   if (!wait_channel_ready()) { SetLastError(ERROR_GEN_FAILURE); return NULL; }
   if (lpStartAddress == NULL || dwCreationFlags != 0) {
      /* CREATE_SUSPENDED and unknown flags are not emulated: silently starting
       * such a thread would violate the caller's synchronization assumptions. */
      SetLastError(ERROR_INVALID_PARAMETER);
      return NULL;
   }

   thread *pThread = (thread *)calloc(1, sizeof(thread));
   if (pThread == NULL) {
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
   }
   pThread->start_routine = lpStartAddress;
   pThread->start_arg = (void *)lpParameter;
   pThread->priority = THREAD_PRIORITY_NORMAL;
   
   struct threads *thread_entry = calloc(1, sizeof(*thread_entry));
   if (thread_entry == NULL) { free(pThread); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   thread_entry->handle = winapi_allocate_handle_token();
   if (thread_entry->handle == NULL) { free(thread_entry); free(pThread); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   pthread_attr_t attr;
   int attr_rc = pthread_attr_init(&attr);
   if (attr_rc != 0) { free(thread_entry); free(pThread); SetLastError(attr_rc == ENOMEM ? ERROR_NOT_ENOUGH_MEMORY : ERROR_INVALID_PARAMETER); return NULL; }
   if (dwStackSize && pthread_attr_setstacksize(&attr, (size_t)dwStackSize) != 0) {
      pthread_attr_destroy(&attr); free(thread_entry); free(pThread); SetLastError(ERROR_INVALID_PARAMETER); return NULL;
   }
   /* Reserve a process-local ID for the child. pthread_t is opaque and cannot
    * be safely truncated to the Win32 DWORD thread-ID contract. */
   pThread->threadId = (DWORD)atomic_fetch_add_explicit(&g_next_thread_id, 1, memory_order_relaxed);
   if (pThread->threadId == 0) pThread->threadId = (DWORD)atomic_fetch_add_explicit(&g_next_thread_id, 1, memory_order_relaxed);
   int rc = pthread_create(&(pThread->pid), &attr, thread_start_routine, pThread);
   pthread_attr_destroy(&attr);

   if(rc == 0) {
      thread_entry->pThread = pThread;
      pthread_mutex_lock(&g_wait_lock); TAILQ_INSERT_TAIL(threads_head, thread_entry, threads_entries); wait_notify_locked(); pthread_mutex_unlock(&g_wait_lock);
      if(lpThreadId != NULL) *lpThreadId = pThread->threadId;
   } else {
      free(thread_entry); free(pThread); pThread = NULL;
      if (rc == EAGAIN || rc == ENOMEM) SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      else if (rc == EPERM) SetLastError(ERROR_ACCESS_DENIED);
      else if (rc == EINVAL) SetLastError(ERROR_INVALID_PARAMETER);
      else SetLastError(ERROR_GEN_FAILURE);
   }

   return rc == 0 ? thread_entry->handle : NULL;
}

DWORD WINAPI GetCurrentThreadId(void)
{
   return current_thread_id();
}

BOOL WINAPI SetThreadPriority(HANDLE hThread, int nPriority)
{
   /* Store the logical Win32 priority and, on Linux, attempt a best-effort
    * mapping to per-thread nice values. Scheduler changes may be denied without
    * privilege; that does not invalidate the logical Win32 priority. */
   switch (nPriority) {
      case THREAD_PRIORITY_IDLE:
      case THREAD_PRIORITY_LOWEST:
      case THREAD_PRIORITY_BELOW_NORMAL:
      case THREAD_PRIORITY_NORMAL:
      case THREAD_PRIORITY_ABOVE_NORMAL:
      case THREAD_PRIORITY_HIGHEST:
      case THREAD_PRIORITY_TIME_CRITICAL:
         break;
      default:
         SetLastError(ERROR_INVALID_PARAMETER);
         return FALSE;
   }
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   struct threads *entry;
   thread *pThread = NULL;
   TAILQ_FOREACH(entry, threads_head, threads_entries) {
      if (entry->handle == hThread) { pThread = entry->pThread; break; }
   }
   if (pThread == NULL) {
      pthread_mutex_unlock(&g_wait_lock);
      SetLastError(ERROR_INVALID_HANDLE);
      return FALSE;
   }
   pThread->priority = nPriority;  /* sempre disponibile come priorità logica Win32 */
#if defined(__linux__) && defined(SYS_gettid)
   if (pThread->osThreadId > 0) {
      /* Best effort: Linux may reject priority increases for unprivileged users.
       * Keep the Win32 logical value even when the scheduler change is denied. */
      (void)setpriority(PRIO_PROCESS, (id_t)pThread->osThreadId,
                        win_priority_to_nice(nPriority));
   }
#endif
   pthread_mutex_unlock(&g_wait_lock);
   SetLastError(ERROR_SUCCESS);
   return TRUE;
}

int WINAPI GetThreadPriority(HANDLE hThread)
{
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   struct threads *entry;
   thread *pThread = NULL;
   TAILQ_FOREACH(entry, threads_head, threads_entries) {
      if (entry->handle == hThread) { pThread = entry->pThread; break; }
   }
   int priority;
   if (pThread == NULL) {
      pthread_mutex_unlock(&g_wait_lock);
      SetLastError(ERROR_INVALID_HANDLE);
      return THREAD_PRIORITY_ERROR_RETURN;
   }
   priority = pThread->priority;
   pthread_mutex_unlock(&g_wait_lock);
   return priority;
}

BOOL WINAPI TerminateThread(HANDLE hThread, DWORD dwExitCode)
{
   (void)hThread; (void)dwExitCode;
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return FALSE;
}


BOOL WINAPI GetExitCodeThread(HANDLE hThread, LPDWORD lpExitCode)
{
   if (lpExitCode == NULL) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   struct threads *entry; thread *pThread=NULL;
   TAILQ_FOREACH(entry, threads_head, threads_entries) if (entry->handle == hThread) { pThread=entry->pThread; break; }
   if (pThread == NULL) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
   *lpExitCode = pThread->finished ? pThread->exitCode : STILL_ACTIVE;
   pthread_mutex_unlock(&g_wait_lock);
   return TRUE;
}

// Suspend a thread by sending it a signal (SIGUSR1), which will
// block the thread until another signal (SIGUSR2) arrives.
//
// Multiple calls to thd_suspend for a single thread have no
// additional effect on the thread -- a single thd_continue
// call will cause it to resume execution.
DWORD WINAPI SuspendThread(HANDLE hThread)
{
   (void)hThread;
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return (DWORD)-1;
}


DWORD WINAPI ResumeThread(HANDLE hThread)
{
   (void)hThread;
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return (DWORD)-1;
}


/***********************************************************************************************/
/*                                            MUTEXES                                          */
/***********************************************************************************************/

HANDLE WINAPI CreateMutex(
  _In_opt_ LPSECURITY_ATTRIBUTES lpMutexAttributes,
  _In_     BOOL                  bInitialOwner,
  _In_opt_ LPCTSTR               lpName
)
{
   
   // Check if the library is initialized.
   if(!winapi_inited()) return NULL;
   if (!wait_channel_ready()) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   if (lpName != NULL && lpName[0] != '\0') { SetLastError(ERROR_CALL_NOT_IMPLEMENTED); return NULL; }

   if(handle_already_exists(lpName))
   {
      SetLastError(ERROR_ALREADY_EXISTS);
      return NULL;
   }

   mutex *pMutex = calloc(1, sizeof(mutex));
   if (pMutex == NULL) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   if (lpName != NULL) {
      size_t name_len = strlen(lpName);
      if (name_len >= sizeof(pMutex->name)) {
         free(pMutex);
         SetLastError(ERROR_INVALID_PARAMETER);
         return NULL;
      }
      memcpy(pMutex->name, lpName, name_len + 1);
   }
   // pMutex->mtx = PTHREAD_RECURSIVE_MUTEX_INITIALIZER;
   struct mutexes *mutex_entry = calloc(1, sizeof(*mutex_entry));
   if (mutex_entry == NULL) {
      free(pMutex);
      SetLastError(ERROR_NOT_ENOUGH_MEMORY);
      return NULL;
   }

   pMutex->pid = (pthread_t)0;
   if (bInitialOwner) { pMutex->pid = pthread_self(); pMutex->locks = 1; }

   mutex_entry->handle = winapi_allocate_handle_token();
   if (mutex_entry->handle == NULL) { free(mutex_entry); free(pMutex); SetLastError(ERROR_NOT_ENOUGH_MEMORY); return NULL; }
   mutex_entry->pMutex = pMutex;
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   TAILQ_INSERT_TAIL(mutexes_head, mutex_entry, mutexes_entries);
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
   return mutex_entry->handle;
}

BOOL WINAPI ReleaseMutex(HANDLE hMutex)
{
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   mutex *pMutex = NULL;
   struct mutexes *entry;
   TAILQ_FOREACH(entry, mutexes_head, mutexes_entries) if (entry->handle == hMutex) { pMutex=entry->pMutex; break; }
   if (pMutex == NULL) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
   if (pMutex->locks == 0 || !pthread_equal(pMutex->pid, pthread_self())) {
      pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_NOT_OWNER); return FALSE;
   }
   pMutex->locks--;
   if (pMutex->locks == 0) pMutex->pid = (pthread_t)0;
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
   return TRUE;
}


/***********************************************************************************************/
/*                                            HANDLES                                          */
/***********************************************************************************************/

static int wait_deadline(DWORD milliseconds, struct timespec *deadline)
{
   if (clock_gettime(g_wait_clock, deadline) != 0) return -1;
   deadline->tv_sec += (time_t)(milliseconds / 1000u);
   deadline->tv_nsec += (long)(milliseconds % 1000u) * 1000000L;
   if (deadline->tv_nsec >= 1000000000L) { deadline->tv_sec++; deadline->tv_nsec -= 1000000000L; }
   return 0;
}

typedef enum { WAIT_KIND_NONE, WAIT_KIND_EVENT, WAIT_KIND_THREAD, WAIT_KIND_MUTEX } wait_kind;
typedef struct { wait_kind kind; void *object; } wait_object;

static int resolve_wait_object_locked(HANDLE h, wait_object *out)
{
   struct events *e; struct threads *t; struct mutexes *m;
   TAILQ_FOREACH(e, events_head, events_entries) if (e->handle == h) { out->kind=WAIT_KIND_EVENT; out->object=e->pEvent; return 0; }
   TAILQ_FOREACH(t, threads_head, threads_entries) if (t->handle == h) { out->kind=WAIT_KIND_THREAD; out->object=t->pThread; return 0; }
   TAILQ_FOREACH(m, mutexes_head, mutexes_entries) if (m->handle == h) { out->kind=WAIT_KIND_MUTEX; out->object=m->pMutex; return 0; }
   return -1;
}

static int object_ready_locked(wait_object *o)
{
   if (o->kind == WAIT_KIND_EVENT) return ((event *)o->object)->signaled != 0;
   if (o->kind == WAIT_KIND_THREAD) return ((thread *)o->object)->finished != FALSE;
   if (o->kind == WAIT_KIND_MUTEX) {
      mutex *m=(mutex *)o->object;
      return m->abandoned || m->locks == 0 || pthread_equal(m->pid, pthread_self());
   }
   return 0;
}

/* Called with g_wait_lock held. Acquires a mutex only after its readiness has
 * been established; all mutex state transitions through this API use this lock. */
static int consume_object_locked(wait_object *o)
{
   if (o->kind == WAIT_KIND_EVENT) {
      event *e=(event *)o->object;
      if (!e->signaled) return 0;
      if (e->autoReset) e->signaled=0;
      return 1;
   }
   if (o->kind == WAIT_KIND_THREAD) return ((thread *)o->object)->finished != FALSE;
   if (o->kind == WAIT_KIND_MUTEX) {
      mutex *m=(mutex *)o->object;
      if (m->locks && !pthread_equal(m->pid, pthread_self())) return 0;
      if (m->locks == 0) m->pid=pthread_self();
      m->locks++;
      m->abandoned = FALSE;
      return 1;
   }
   return 0;
}

DWORD WINAPI WaitForMultipleObjects(DWORD nCount, const HANDLE *lpHandles,
                                    BOOL bWaitAll, DWORD dwMilliseconds)
{
   if (!winapi_inited() || lpHandles == NULL || nCount == 0 || nCount > 64) {
      SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED;
   }
   wait_object objects[64];
   struct timespec deadline;
   int timed = dwMilliseconds != INFINITE;
   if (!wait_channel_ready()) { SetLastError(ERROR_NOT_ENOUGH_MEMORY); return WAIT_FAILED; }
   if (timed && wait_deadline(dwMilliseconds, &deadline) != 0) { SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED; }

   pthread_mutex_lock(&g_wait_lock);
   for (DWORD i=0;i<nCount;i++) {
      if (!lpHandles[i] || lpHandles[i] == INVALID_HANDLE_VALUE || resolve_wait_object_locked(lpHandles[i], &objects[i]) != 0) {
         pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED;
      }
      for (DWORD j=0;j<i;j++) if (lpHandles[i] == lpHandles[j]) {
         pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_INVALID_PARAMETER); return WAIT_FAILED;
      }
   }
   /* Pin each object while sleeping; CloseHandle refuses to destroy pinned objects. */
   for (DWORD i=0;i<nCount;i++) {
      if(objects[i].kind==WAIT_KIND_EVENT) ((event*)objects[i].object)->active_waiters++;
      else if(objects[i].kind==WAIT_KIND_THREAD) ((thread*)objects[i].object)->active_waiters++;
      else ((mutex*)objects[i].object)->active_waiters++;
   }

   DWORD result=WAIT_FAILED;
   for (;;) {
      if (bWaitAll) {
         DWORD ready=0;
         for(DWORD i=0;i<nCount;i++) ready += object_ready_locked(&objects[i]) ? 1u : 0u;
         if(ready==nCount) {
            DWORD abandoned_index = nCount;
            /* All predicates are checked while transitions are excluded by the same lock.
             * Win32 reports WAIT_ABANDONED_0 plus the lowest abandoned mutex index. */
            for(DWORD i=0;i<nCount;i++) {
               if(abandoned_index == nCount && objects[i].kind==WAIT_KIND_MUTEX &&
                  ((mutex*)objects[i].object)->abandoned) abandoned_index=i;
               (void)consume_object_locked(&objects[i]);
            }
            result=abandoned_index != nCount ? WAIT_ABANDONED_0 + abandoned_index : WAIT_OBJECT_0; break;
         }
      } else {
         int found=0;
         for(DWORD i=0;i<nCount;i++) if(object_ready_locked(&objects[i])) {
            BOOL abandoned = objects[i].kind==WAIT_KIND_MUTEX && ((mutex*)objects[i].object)->abandoned;
            if(consume_object_locked(&objects[i])) { result=abandoned ? WAIT_ABANDONED_0+i : WAIT_OBJECT_0+i; found=1; break; }
         }
         if(found) break;
      }
      if(dwMilliseconds==0) { result=WAIT_TIMEOUT; break; }
      /* pthread_cond_timedwait may return ETIMEDOUT even when a predicate
       * became true just before the mutex was reacquired. Loop once more so
       * readiness is always checked before the expired deadline is honored. */
      if (timed) {
         struct timespec now;
         if (clock_gettime(g_wait_clock, &now) != 0) {
            SetLastError(ERROR_GEN_FAILURE); result=WAIT_FAILED; break;
         }
         if (now.tv_sec > deadline.tv_sec ||
             (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            result=WAIT_TIMEOUT; break;
         }
      }
      int rc = timed ? pthread_cond_timedwait(&g_wait_cond, &g_wait_lock, &deadline)
                     : pthread_cond_wait(&g_wait_cond, &g_wait_lock);
      if(rc==ETIMEDOUT) continue;
      if(rc!=0) { SetLastError(ERROR_GEN_FAILURE); result=WAIT_FAILED; break; }
   }
   for(DWORD i=0;i<nCount;i++) {
      if(objects[i].kind==WAIT_KIND_EVENT) ((event*)objects[i].object)->active_waiters--;
      else if(objects[i].kind==WAIT_KIND_THREAD) ((thread*)objects[i].object)->active_waiters--;
      else ((mutex*)objects[i].object)->active_waiters--;
   }
   wait_notify_locked();
   pthread_mutex_unlock(&g_wait_lock);
   return result;
}

DWORD WINAPI WaitForSingleObject(HANDLE hHandle, DWORD dwMilliseconds)
{
   DWORD r=WaitForMultipleObjects(1, &hHandle, TRUE, dwMilliseconds);
   return r;
}

BOOL WINAPI CloseHandle(HANDLE hObject)
{
   if (!winapi_inited()) return FALSE;
   if (hObject == NULL || hObject == INVALID_HANDLE_VALUE) { SetLastError(ERROR_INVALID_HANDLE); return FALSE; }
   (void)wait_channel_ready();
   pthread_mutex_lock(&g_wait_lock);
   struct events *e;
   TAILQ_FOREACH(e, events_head, events_entries) if (e->handle == hObject) {
      event *obj=e->pEvent;
      if(obj->active_waiters) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_BUSY); return FALSE; }
      TAILQ_REMOVE(events_head,e,events_entries);
      int backing_fd = obj->fd;
      BOOL named = obj->name[0] != '\0';
      pthread_mutex_unlock(&g_wait_lock);
      if(named) {
         if(munmap(obj,sizeof(*obj))!=0) {
            /* Closing is transactional: if the backing mapping cannot be
             * released, restore the registry entry so the still-live handle
             * remains usable and can be closed again. */
            pthread_mutex_lock(&g_wait_lock);
            TAILQ_INSERT_TAIL(events_head,e,events_entries);
            pthread_mutex_unlock(&g_wait_lock);
            SetLastError(ERROR_GEN_FAILURE);
            return FALSE;
         }
         if(backing_fd>=0) (void)close(backing_fd);
      } else { free(obj); }
      free(e); return TRUE;
   }
   struct threads *t;
   TAILQ_FOREACH(t, threads_head, threads_entries) if (t->handle == hObject) {
      thread *obj=t->pThread;
      if(obj->active_waiters) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_BUSY); return FALSE; }
      if(!obj->finished) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_BUSY); return FALSE; }
      TAILQ_REMOVE(threads_head,t,threads_entries);
      pthread_mutex_unlock(&g_wait_lock);
      {
         int join_rc = pthread_join(obj->pid,NULL);
         if(join_rc != 0) {
            /* Do not discard the only registry record when the native
             * resource has not been confirmed joined. */
            pthread_mutex_lock(&g_wait_lock);
            TAILQ_INSERT_TAIL(threads_head,t,threads_entries);
            pthread_mutex_unlock(&g_wait_lock);
            SetLastError(ERROR_GEN_FAILURE);
            return FALSE;
         }
      }
      free(obj); free(t); return TRUE;
   }
   struct mutexes *m;
   TAILQ_FOREACH(m, mutexes_head, mutexes_entries) if (m->handle == hObject) {
      mutex *obj=m->pMutex;
      if(obj->active_waiters || obj->locks) { pthread_mutex_unlock(&g_wait_lock); SetLastError(ERROR_BUSY); return FALSE; }
      TAILQ_REMOVE(mutexes_head,m,mutexes_entries);
      pthread_mutex_unlock(&g_wait_lock);
      free(obj); free(m); return TRUE;
   }
   pthread_mutex_unlock(&g_wait_lock);
   {
      int file_result = wfile_close_handle(hObject);
      if (file_result > 0) return TRUE;
      if (file_result < 0) return FALSE;
   }
#if defined(ENABLE_WSHMEM) && ENABLE_WSHMEM
   {
      int shmem_result = wshmem_close_handle(hObject);
      if (shmem_result > 0) return TRUE;
      if (shmem_result < 0) return FALSE;
   }
#endif
   SetLastError(ERROR_INVALID_HANDLE);
   return FALSE;
}

#endif