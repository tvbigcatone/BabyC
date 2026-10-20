#include "windows.h"
#include "wprivate.h"
#include <stdatomic.h>


/* One process-wide token namespace for opaque handles allocated by the
 * library. Tokens are monotonically increasing and never reused. Saturate at
 * exhaustion instead of allowing atomic_fetch_add to wrap and eventually
 * recycle stale tokens. */
static atomic_uintptr_t g_next_handle_token = ATOMIC_VAR_INIT((uintptr_t)0x10000);
HANDLE winapi_allocate_handle_token(void)
{
   uintptr_t id = atomic_load_explicit(&g_next_handle_token, memory_order_relaxed);
   for (;;) {
      if (id < (uintptr_t)0x10000 || id == UINTPTR_MAX) return NULL;
      if (atomic_compare_exchange_weak_explicit(&g_next_handle_token, &id, id + 1,
                                                memory_order_relaxed,
                                                memory_order_relaxed))
         return (HANDLE)id;
   }
}

static pthread_once_t winapi_init_once = PTHREAD_ONCE_INIT;
static void init_winapi(void);

/* Inizializzazione thread-safe e idempotente della libreria. */
BOOL winapi_inited(void)
{
   pthread_once(&winapi_init_once, init_winapi);
   return TRUE;
}

extern void thread_init_suspend_routine(void);
extern void fsearchs_init(void);

//---------------------------------------------------------------------------------------------//
struct _eventsHead eventsHead;
struct _threadsHead threadsHead;
struct _mutexesHead mutexesHead;
struct _mapFilesHead mapFilesHead;
struct _fileViewsHead fileViewsHead;
struct _fileSearchsHead fileSearchsHead;
// struct _disksHead disksHead;

struct _eventsHead  *events_head  = &eventsHead;  
struct _threadsHead *threads_head = &threadsHead; 
struct _mutexesHead *mutexes_head = &mutexesHead; 
struct _fileViewsHead *fileviews_head = &fileViewsHead; 
struct _mapFilesHead  *mapfiles_head  = &mapFilesHead;
struct _fileSearchsHead *filesearchs_head = &fileSearchsHead; 
// struct _disksHead       *disks_head       = &disksHead;
//---------------------------------------------------------------------------------------------//

static void init_winapi(void) {

#if(ENABLE_WTHREADS == 1)
   TAILQ_INIT(events_head);
   TAILQ_INIT(threads_head);
   TAILQ_INIT(mutexes_head);

   thread_init_suspend_routine();
#endif
   TAILQ_INIT(mapfiles_head);
   TAILQ_INIT(fileviews_head);
   
   TAILQ_INIT(filesearchs_head);
   // TAILQ_INIT(disks_head);
   fsearchs_init();

   return;
}

BOOL handle_already_exists(const char *name)
{
   if(name == NULL || (strcmp(name, "") == 0)) return FALSE;

   //---------------------------------------------------------------------------------------------//
   struct events *event_entry;
   TAILQ_FOREACH(event_entry, events_head, events_entries) 
   {
      if(strcmp(event_entry->pEvent->name, name) == 0)
         return TRUE;
   }
   //---------------------------------------------------------------------------------------------//
   struct mutexes *mutex_entry;
   TAILQ_FOREACH(mutex_entry, mutexes_head, mutexes_entries) 
   {
      if(strcmp(mutex_entry->pMutex->name, name) == 0)
         return TRUE;
   }
   //---------------------------------------------------------------------------------------------//
   struct mapfiles *mapfile_entry;
   TAILQ_FOREACH(mapfile_entry, mapfiles_head, mapfiles_entries) 
   {
      if(strcmp(mapfile_entry->pMapFile->name, name) == 0)
         return TRUE;
   }

   return FALSE;
}