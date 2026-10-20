#ifndef WINAPI_WTHREADS_H
#define WINAPI_WTHREADS_H


#define INFINITE	                     0xFFFFFFFF

#define WAIT_OBJECT_0                  0
#define WAIT_ABANDONED_0               128
#define WAIT_TIMEOUT                   0x00000102L
#define WAIT_ABANDONED                 0x00000080L
#define WAIT_FAILED                    0xFFFFFFFFL

//
// Thread priorities
//

#define THREAD_PRIORITY_IDLE            -15
#define THREAD_PRIORITY_LOWEST          -2
#define THREAD_PRIORITY_BELOW_NORMAL    -1
#define THREAD_PRIORITY_NORMAL          0
#define THREAD_PRIORITY_ABOVE_NORMAL    1
#define THREAD_PRIORITY_HIGHEST         2
#define THREAD_PRIORITY_TIME_CRITICAL   15
#define THREAD_PRIORITY_ERROR_RETURN     0x7fffffff

//
// Thread creation flags
//

#define CREATE_SUSPENDED                0x00000004

#define STATUS_SUCCESS                  0x00000000
#define STATUS_INVALID_HANDLE           0xC0000008
#define STATUS_PENDING                  0x00000103 
#define STATUS_UNSUCCESSFUL             0xC0000001
#define STILL_ACTIVE                    STATUS_PENDING               // 0x103
#define MAX_THREADS                     256


#define MUTEX_ALL_ACCESS	0x1f0001
#define MUTEX_MODIFY_STATE	1
#define SEMAPHORE_ALL_ACCESS	0x1f0003
#define SEMAPHORE_MODIFY_STATE	2
#define EVENT_ALL_ACCESS	0x1f0003
#define EVENT_MODIFY_STATE	2
#define PIPE_ACCESS_DUPLEX      3
#define PIPE_ACCESS_INBOUND     1
#define PIPE_ACCESS_OUTBOUND    2
#define PIPE_TYPE_BYTE	0
#define PIPE_TYPE_MESSAGE	4
#define PIPE_READMODE_BYTE	0
#define PIPE_READMODE_MESSAGE	2
#define PIPE_WAIT	0
#define PIPE_NOWAIT	1
#define PIPE_CLIENT_END 0
#define PIPE_SERVER_END 1
#define PIPE_UNLIMITED_INSTANCES 255

typedef DWORD (__stdcall *LPTHREAD_START_ROUTINE) (
    LPVOID lpThreadParameter
);

/***********************************************************************************************/
/*                                   CRITICAL SECTION                                          */
/***********************************************************************************************/

typedef pthread_mutex_t    CRITICAL_SECTION;
typedef CRITICAL_SECTION*  LPCRITICAL_SECTION;
typedef CRITICAL_SECTION*  PCRITICAL_SECTION;

void WINAPI InitializeCriticalSection(
  _Out_ LPCRITICAL_SECTION lpCriticalSection
);

void WINAPI EnterCriticalSection(
  _Inout_ LPCRITICAL_SECTION lpCriticalSection
);

void WINAPI LeaveCriticalSection(
  _Inout_ LPCRITICAL_SECTION lpCriticalSection
);

void WINAPI DeleteCriticalSection(
  _Inout_ LPCRITICAL_SECTION lpCriticalSection
);

/***********************************************************************************************/
/*                               WINTHREADS FUNCTIONS                                          */
/***********************************************************************************************/

HANDLE WINAPI CreateEvent(
  _In_opt_ LPSECURITY_ATTRIBUTES lpEventAttributes,
  _In_     BOOL                  bManualReset,
  _In_     BOOL                  bInitialState,
  _In_opt_ LPCTSTR               lpName
);

HANDLE WINAPI OpenEvent(
  _In_ DWORD   dwDesiredAccess,
  _In_ BOOL    bInheritHandle,
  _In_ LPCTSTR lpName
);

BOOL WINAPI SetEvent(
  _In_ HANDLE hEvent
);

BOOL WINAPI ResetEvent(
  _In_ HANDLE hEvent
);

BOOL WINAPI PulseEvent(
  _In_ HANDLE hEvent
);

HANDLE WINAPI CreateThread(
  _In_opt_  LPSECURITY_ATTRIBUTES  lpThreadAttributes,
  _In_      SIZE_T                 dwStackSize,
  _In_      LPTHREAD_START_ROUTINE lpStartAddress,
  _In_opt_  LPVOID                 lpParameter,
  _In_      DWORD                  dwCreationFlags,
  _Out_opt_ LPDWORD                lpThreadId
);

BOOL WINAPI SetThreadPriority(
  _In_ HANDLE hThread,
  _In_ int    nPriority
);

int WINAPI GetThreadPriority(_In_ HANDLE hThread);

DWORD WINAPI GetCurrentThreadId(void);

BOOL WINAPI TerminateThread(
  _Inout_ HANDLE hThread,
  _In_    DWORD  dwExitCode
);

BOOL WINAPI GetExitCodeThread(
  _In_  HANDLE  hThread,
  _Out_ LPDWORD lpExitCode
);

DWORD WINAPI SuspendThread(
  _In_ HANDLE hThread
);

DWORD WINAPI ResumeThread(
  _In_ HANDLE hThread
);

HANDLE WINAPI CreateMutex(
  _In_opt_ LPSECURITY_ATTRIBUTES lpMutexAttributes,
  _In_     BOOL                  bInitialOwner,
  _In_opt_ LPCTSTR               lpName
);

BOOL WINAPI ReleaseMutex(
  _In_ HANDLE hMutex
);

DWORD WINAPI WaitForSingleObject(
  _In_ HANDLE hHandle,
  _In_ DWORD  dwMilliseconds
);

DWORD WINAPI WaitForMultipleObjects(
  _In_       DWORD  nCount,
  _In_ const HANDLE *lpHandles,
  _In_       BOOL   bWaitAll,
  _In_       DWORD  dwMilliseconds
);

BOOL WINAPI CloseHandle(
  _In_ HANDLE hObject
);

#endif /* WINAPI_WTHREADS_H */
