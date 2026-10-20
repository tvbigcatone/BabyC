
#ifndef __WINDEF_H__
#define __WINDEF_H__


// #define  __cdecl     __attribute__((cdecl))
// #define  __stdcall   __attribute__((stdcall))
// #define  __fastcall  __attribute__((fastcall))
#define  __stdcall  /* nothing */
#define  WINAPI __stdcall
#define  far        /* nothing */
#define  near       /* nothing */
#define  FAR  far        
#define  NEAR near 

#define __in        /* nothing */
#define __out       /* nothing */
#define __inout     /* nothing */
#define __in_opt    /* nothing */
#define __out_opt   /* nothing */
#define __deref_out_range(A,B)
#define __nullterminated
#define _In_      __in
#define _Out_     __out
#define _Inout_   __inout  
#define _In_opt_  __in_opt
#define _Out_opt_ __out_opt

#define UNREFERENCED_PARAMETER(e) do { (void)(e); } while (0)

#define INLINE inline
#define __inline inline

/***********************************************************************************************/
/*                                         TYPEDEFS                                            */
/***********************************************************************************************/

typedef int8_t             __int8; 
typedef int16_t            __int16; 
typedef int32_t            __int32; 
typedef int64_t            __int64; 

typedef int                BOOL;   
typedef BOOL*              LPBOOL; 
typedef BOOL*              PBOOL;         
#define TRUE               1
#define FALSE              0

#ifndef BOOLEAN
typedef BOOL               BOOLEAN;
#endif

typedef  size_t            SIZE_T;
typedef  ssize_t           SSIZE_T;

#ifndef DWORD
typedef uint32_t           DWORD;
#endif
typedef DWORD*             LPDWORD;
typedef DWORD*             PDWORD;
#ifndef WORD
typedef unsigned short     WORD;
#endif
typedef WORD*              LPWORD;
typedef WORD*              PWORD;

#ifndef BYTE
typedef unsigned char       BYTE;
#endif
typedef BYTE*              LPBYTE;
typedef BYTE*              PBYTE;

#ifndef SHORT
typedef short              SHORT;
#endif
typedef SHORT*             LPSHORT;
typedef SHORT*             PSHORT;
#ifndef USHORT
typedef unsigned short     USHORT;
#endif
typedef USHORT*            LPUSHORT;
typedef USHORT*            PUSHORT;

#ifndef LONG
typedef int32_t            LONG;
#endif
typedef LONG*              LPLONG;
typedef LONG*              PLONG;
#ifndef ULONG
typedef uint32_t           ULONG;
#endif
typedef ULONG*             LPULONG;
typedef ULONG*             PULONG;
typedef int64_t            LONGLONG;
typedef LONGLONG*          LPLONGLONG;
typedef LONGLONG*          PLONGLONG;
#ifndef ULONGLONG
typedef uint64_t           ULONGLONG;
#endif
typedef ULONGLONG*         LPULONGLONG;
typedef ULONGLONG*         PULONGLONG;
#ifndef DWORDLONG
typedef uint64_t           DWORDLONG;
#endif
typedef DWORDLONG*         LPUDWORDLONG;
typedef DWORDLONG*         PDWORDLONG;

typedef uintptr_t           ULONG_PTR;
typedef ULONG_PTR          DWORD_PTR;
typedef intptr_t           LONG_PTR;

#ifndef INT
typedef int                INT;
#endif
typedef INT*               LPINT;
typedef INT*               PINT;
#ifndef UINT
typedef unsigned int       UINT;
#endif
typedef UINT*              LPUINT;
typedef UINT*              PUINT;

#ifndef VOID
typedef void               VOID;
#endif
typedef void*              LPVOID;
typedef void*              PVOID;
typedef const void         CVOID;
typedef const void*        LPCVOID;
typedef const void*        PCVOID;

// typedef hmodule_t          HMODULE;
typedef void*              FARPROC;
typedef int                LCID;
typedef int                HKEY;
typedef HKEY*              PHKEY;
typedef int                REGSAM;
typedef int                HWND;
typedef int                MMRESULT;
typedef int                TOKEN_INFORMATION_CLASS;

// typedef void*              LPCRITICAL_SECTION;
typedef void*              LPSECURITY_ATTRIBUTES;
typedef void*              LPSTARTUPINFO;
typedef void*              LPPROCESS_INFORMATION;
typedef void*              LPTIME_ZONE_INFORMATION;
typedef void*              PINPUT_RECORD;
typedef void*              PHANDLER_ROUTINE;
typedef void*              LPWSAOVERLAPPED;
typedef void*              LPWSAOVERLAPPED_COMPLETION_ROUTINE;
typedef void*              PSID;
typedef void*              PSID_IDENTIFIER_AUTHORITY;
typedef void*              PACL;
typedef void*              PSECURITY_DESCRIPTOR;
typedef void*              LPSERVICE_TABLE_ENTRY;
typedef void*              LPSERVICE_STATUS;
typedef void*              HCRYPTPROV;
typedef void*              LPOVERLAPPED;

#ifndef NTSTATUS
#define NTSTATUS           LONG
#endif
typedef int                SERVICE_STATUS_HANDLE;

typedef  char*             LPSTR;
typedef  char*             PSTR;
typedef  const char*       LPCSTR;
typedef  const char*       PCSTR;

#ifdef _UNICODE
typedef  wchar_t           TCHAR;
#else
typedef  char              TCHAR;
#endif
typedef  TCHAR*            LPTSTR;
typedef  const TCHAR*      LPCTSTR;

typedef  wchar_t           WCHAR;
typedef  WCHAR*            LPWSTR;
typedef  WCHAR*            PWSTR;
typedef  const WCHAR*      LPCWSTR;
typedef  const WCHAR*      PCWSTR;


typedef  void*             HANDLE;
typedef  HANDLE*           PHANDLE;
typedef  HANDLE*           LPHANDLE;

#ifndef INVALID_HANDLE_VALUE
// #define INVALID_HANDLE_VALUE (-1)
#define  INVALID_HANDLE_VALUE ((HANDLE) -1L)
#endif
#ifndef INVALID_HANDLE
#define  INVALID_HANDLE ((intptr_t)-1)
#endif

typedef  void*             HINSTANCE;
typedef  HINSTANCE*        PHINSTANCE;
typedef  HINSTANCE*        LPHINSTANCE;

/***********************************************************************************************/
/*                                          STRUCTS                                             */
/***********************************************************************************************/

typedef struct _FILETIME {
  DWORD dwLowDateTime;
  DWORD dwHighDateTime;
} FILETIME, *PFILETIME, *LPFILETIME;

typedef union _LARGE_INTEGER {
  struct {
    DWORD LowPart;
    LONG  HighPart;
  };
  struct {
    DWORD LowPart;
    LONG  HighPart;
  } u;
  LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER, *LPLARGE_INTEGER;

typedef union _ULARGE_INTEGER {
  struct {
    DWORD LowPart;
    DWORD HighPart;
  };
  struct {
    DWORD LowPart;
    DWORD HighPart;
  } u;
  ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER, *LPULARGE_INTEGER;

typedef struct _RECT {
   LONG left;
   LONG top;
   LONG right;
   LONG bottom;
} RECT, *PRECT, *LPRECT;

typedef struct _POINT{
   int x;
   int y;
} POINT, *PPOINT, *LPPOINT;

typedef struct _LSA_UNICODE_STRING {
  USHORT Length;
  USHORT MaximumLength;
  PWSTR  Buffer;
} LSA_UNICODE_STRING, *PLSA_UNICODE_STRING, UNICODE_STRING, *PUNICODE_STRING;

/***********************************************************************************************/
/*                                          CONSTS                                             */
/***********************************************************************************************/

#define MAXWORD     0xffff
#define MAXDWORD    0xffffffff

/* In C++ le macro min/max rompono <algorithm>/<cmath>: le forniamo solo in C. */
#ifndef __cplusplus
#ifndef min
#define min(a,b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef max
#define max(a,b) (((a) > (b)) ? (a) : (b))
#endif
#endif
#endif