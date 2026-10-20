/* windows.h interpreter bridge (NOT a win.h API surface).
 *
 * Official guest include: #include <windows.h> → WinSetupFunc.
 *  - WIN32 branch: thin wrappers over native Win32.
 *  - Linux branch: wrappers over libWINAPI (src/winapi/).
 * win.h is intentionally not registered; it is not an official Windows header.
 * New Win32 APIs belong in libWINAPI first, then a thin Intrinsic here.
 */
#include "winerror_defs.h"
#ifdef WIN32
#include "../interpreter.h"
#include "../picoc.h"
#include "../platform/guest_thread.h"

//#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <shellapi.h>
#include <string.h>

#include "keyb.c"

/* Equivalent des #define NAME VALUE */
static int S_SW_FORCEMINIMIZE = SW_FORCEMINIMIZE ;
static int S_SW_HIDE = SW_HIDE ;
static int S_SW_MAXIMIZE = SW_MAXIMIZE ;
static int S_SW_MINIMIZE = SW_MINIMIZE ;
static int S_SW_RESTORE = SW_RESTORE ;
static int S_SW_SHOW = SW_SHOW ;
static int S_SW_SHOWDEFAULT = SW_SHOWDEFAULT ;
static int S_SW_SHOWMAXIMIZED = SW_SHOWMAXIMIZED ;
static int S_SW_SHOWMINIMIZED = SW_SHOWMINIMIZED ;
static int S_SW_SHOWMINNOACTIVE = SW_SHOWMINNOACTIVE ;
static int S_SW_SHOWNA = SW_SHOWNA ;
static int S_SW_SHOWNOACTIVATE = SW_SHOWNOACTIVATE ;
static int S_SW_SHOWNORMAL = SW_SHOWNORMAL ;

static int S_HWND_BOTTOM = 1 ;
static int S_HWND_NOTOPMOST = -2 ;
static int S_HWND_TOP = 0 ;
static int S_HWND_TOPMOST = -1 ;
static int S_SWP_ASYNCWINDOWPOS = SWP_ASYNCWINDOWPOS ;
static int S_SWP_DEFERERASE = SWP_DEFERERASE ;
static int S_SWP_DRAWFRAME = SWP_DRAWFRAME ;
static int S_SWP_FRAMECHANGED = SWP_FRAMECHANGED ;
static int S_SWP_HIDEWINDOW = SWP_HIDEWINDOW ;
static int S_SWP_NOACTIVATE = SWP_NOACTIVATE ;
static int S_SWP_NOCOPYBITS = SWP_NOCOPYBITS ;
static int S_SWP_NOMOVE = SWP_NOMOVE ;
static int S_SWP_NOOWNERZORDER = SWP_NOOWNERZORDER ;
static int S_SWP_NOREDRAW = SWP_NOREDRAW ;
static int S_SWP_NOREPOSITION = SWP_NOREPOSITION ;
static int S_SWP_NOSENDCHANGING = SWP_NOSENDCHANGING ;
static int S_SWP_NOSIZE = SWP_NOSIZE ;
static int S_SWP_NOZORDER = SWP_NOZORDER ;
static int S_SWP_SHOWWINDOW = SWP_SHOWWINDOW ;

static int S_WM_COMMAND = WM_COMMAND ;
static int S_WM_SYSCOMMAND = WM_SYSCOMMAND ;
static int S_SC_MAXIMIZE = SC_MAXIMIZE ;
static int S_SC_MINIMIZE = SC_MINIMIZE ;
static int S_SC_CLOSE = SC_CLOSE ;

static int S_MB_OK = MB_OK;
static int S_MB_OKCANCEL = MB_OKCANCEL;

/* Console / ANSI (safe when embedded: APIs return 0 if no console) */
#ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004
#endif
#ifndef DISABLE_NEWLINE_AUTO_RETURN
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008
#endif
static int S_STD_INPUT_HANDLE  = (int)(INT_PTR)STD_INPUT_HANDLE;
static int S_STD_OUTPUT_HANDLE = (int)(INT_PTR)STD_OUTPUT_HANDLE;
static int S_STD_ERROR_HANDLE  = (int)(INT_PTR)STD_ERROR_HANDLE;
static int S_ENABLE_VIRTUAL_TERMINAL_PROCESSING = ENABLE_VIRTUAL_TERMINAL_PROCESSING;
static int S_DISABLE_NEWLINE_AUTO_RETURN = DISABLE_NEWLINE_AUTO_RETURN;
static int S_MB_YESNO = MB_YESNO;
static int S_MB_YESNOCANCEL = MB_YESNOCANCEL;
static int S_MB_ICONERROR = MB_ICONERROR;
static int S_MB_ICONWARNING = MB_ICONWARNING;
static int S_MB_ICONINFORMATION = MB_ICONINFORMATION;
static int S_MB_ICONQUESTION = MB_ICONQUESTION;
static int S_IDOK = IDOK;
static int S_IDCANCEL = IDCANCEL;
static int S_IDYES = IDYES;
static int S_IDNO = IDNO;


/* handy structure definitions */
/*typedef struct hwnd { HWND * ptr ; } HWNDStruct ;*/
int matchpattern( const char * pattern, const char * st) ;

/* define definition */
const char WinDefs[] = "\
typedef struct __HWNDStruct HWND ;\n\
typedef void * HANDLE ;\n\
typedef unsigned int DWORD ;\n\
typedef char * LPCTSTR ;\n\
typedef char * LPTSTR ;\n\
typedef void * LPVOID ;\n\
typedef const void * LPCVOID ;\n\
typedef DWORD * LPDWORD ;\n\
typedef int LONG ;\n\
typedef int * PLONG ;\n\
typedef unsigned short WORD ;\n\
typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;\n\
typedef struct _SYSTEMTIME { WORD wYear; WORD wMonth; WORD wDayOfWeek; WORD wDay; WORD wHour; WORD wMinute; WORD wSecond; WORD wMilliseconds; } SYSTEMTIME;\n\
typedef struct _LARGE_INTEGER { long long QuadPart; } LARGE_INTEGER;\n\
typedef struct _ULARGE_INTEGER { unsigned long long QuadPart; } ULARGE_INTEGER;\n\
typedef ULARGE_INTEGER *PULARGE_INTEGER;\n\
typedef unsigned long long DWORDLONG;\n\
typedef struct _MEMORYSTATUSEX { DWORD dwLength; DWORD dwMemoryLoad; DWORDLONG ullTotalPhys; DWORDLONG ullAvailPhys; DWORDLONG ullTotalPageFile; DWORDLONG ullAvailPageFile; DWORDLONG ullTotalVirtual; DWORDLONG ullAvailVirtual; DWORDLONG ullAvailExtendedVirtual; } MEMORYSTATUSEX;\n\
typedef MEMORYSTATUSEX *LPMEMORYSTATUSEX;\n\
typedef struct _WIN32_FIND_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime; DWORD nFileSizeHigh; DWORD nFileSizeLow; DWORD dwReserved0; DWORD dwReserved1; char cFileName[260]; char cAlternateFileName[14]; } WIN32_FIND_DATA;\n\
typedef unsigned int UINT ;\n\
typedef unsigned int WPARAM ;\n\
typedef long LPARAM ;\n\
typedef long LRESULT ;\n\
#define GENERIC_READ 0x80000000u\n\
#define GENERIC_WRITE 0x40000000u\n\
#define FILE_SHARE_READ 1u\n\
#define FILE_SHARE_WRITE 2u\n\
#define FILE_SHARE_DELETE 4u\n\
#define CREATE_NEW 1u\n\
#define CREATE_ALWAYS 2u\n\
#define OPEN_EXISTING 3u\n\
#define OPEN_ALWAYS 4u\n\
#define TRUNCATE_EXISTING 5u\n\
#define FILE_ATTRIBUTE_NORMAL 0x80u\n\
#define FILE_BEGIN 0u\n\
#define FILE_CURRENT 1u\n\
#define FILE_END 2u\n\
#define INVALID_HANDLE_VALUE ((HANDLE)-1)\n\
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200u\n#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000u\n\
#define INFINITE 0xffffffffu\n#define WAIT_OBJECT_0 0u\n#define WAIT_TIMEOUT 0x102u\n#define WAIT_FAILED 0xffffffffu\n\
#define EVENT_MODIFY_STATE 2u\n#define EVENT_ALL_ACCESS 0x1f0003u\n#define MUTEX_MODIFY_STATE 1u\n#define MUTEX_ALL_ACCESS 0x1f0001u\n\
#define PAGE_READONLY 2u\n#define PAGE_READWRITE 4u\n#define FILE_MAP_COPY 1u\n#define FILE_MAP_WRITE 2u\n#define FILE_MAP_READ 4u\n#define FILE_MAP_ALL_ACCESS 0xf001fu\n\
#define DRIVE_UNKNOWN 0u\n#define DRIVE_NO_ROOT_DIR 1u\n#define DRIVE_REMOVABLE 2u\n#define DRIVE_FIXED 3u\n#define DRIVE_REMOTE 4u\n#define DRIVE_CDROM 5u\n#define DRIVE_RAMDISK 6u\n\
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004u\n\
#define DISABLE_NEWLINE_AUTO_RETURN 0x0008u\n\
#define MAX_PATH 260\n\
";

void WinMsgBox(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	MessageBox( NULL, Param[1]->Val->Pointer, Param[0]->Val->Pointer, MB_OK ) ;
}
void WinGetForegroundWindow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Pointer = GetForegroundWindow() ;
}
void WinGetWindowText(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	GetWindowTextA( (HWND)Param[0]->Val->Pointer, (LPSTR)Param[1]->Val->Pointer, Param[2]->Val->Integer ) ; 
}
void WinShowWindow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = ShowWindow( Param[0]->Val->Pointer, Param[1]->Val->Integer ) ;
}
void WinFindWindow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Pointer = FindWindow( Param[0]->Val->Pointer, Param[1]->Val->Pointer ) ;
}
void WindowActive( HWND hwnd ) {
	if ( hwnd!=0 ) {
		if( IsIconic( hwnd ) ) ShowWindow( hwnd, SW_RESTORE ) ;
		else ShowWindow(hwnd, SW_SHOW);
		SetForegroundWindow( hwnd ) ;
		SetActiveWindow( hwnd ) ;
		SetCapture( hwnd ) ;
		}	
	}
void WinActiveWindow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	WindowActive( Param[0]->Val->Pointer ) ;
}

BOOL CALLBACK EnumWndProcActive( HWND hwnd, LPARAM pPattern ) {
	char title[1024] ;
	if( hwnd != 0 ) if( IsWindow( hwnd ) ) {
		if( GetWindowTextA( hwnd, title, 1024 ) ) {
			if( strlen( title ) > 0 ) 
			if( matchpattern( (char*)pPattern, title ) ) {
				if( IsIconic( hwnd ) ) ShowWindow( hwnd, SW_RESTORE ) ;
				else ShowWindow(hwnd, SW_SHOW);
				SetActiveWindow( hwnd );
				SetForegroundWindow ( hwnd );
				return FALSE ;
				}
			}
		}
	return TRUE ;
	}
int WindowActivePattern(LPCTSTR Name) {
	EnumWindows(EnumWndProcActive,(LPARAM)Name) ;
	return 0 ;
	}
void WinActiveWindowWithPattern(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = WindowActivePattern( Param[0]->Val->Pointer ) ;
}
void WinCloseWindow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	SendMessage( Param[0]->Val->Pointer, WM_SYSCOMMAND, SC_CLOSE, (LPARAM)NULL );
	}
	
void WinBringWindowToTop(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = BringWindowToTop( Param[0]->Val->Pointer ) ;
}

void WinSetWindowPos(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	HWND hWndInsertAfter ;
	switch((int)Param[1]->Val->Pointer) {
		case 1: hWndInsertAfter = HWND_BOTTOM ; break ;
		case -2: hWndInsertAfter = HWND_NOTOPMOST ; break ;
		case 0: hWndInsertAfter = HWND_TOP ; break ;
		case -1: hWndInsertAfter = HWND_TOPMOST ; break ;
		default: hWndInsertAfter = Param[1]->Val->Pointer ; break ;
	}
	ReturnValue->Val->Integer = SetWindowPos( Param[0]->Val->Pointer, hWndInsertAfter, Param[2]->Val->Integer, Param[3]->Val->Integer, Param[4]->Val->Integer, Param[5]->Val->Integer, Param[6]->Val->Integer ) ;
}
void WinSendMessage(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = SendMessage( Param[0]->Val->Pointer, Param[1]->Val->Integer, Param[2]->Val->Integer, Param[3]->Val->Integer ) ;
}
void WinSleep(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	Sleep( Param[0]->Val->Integer ) ;
}

void WinWindowMaximize(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)  {
	SendMessage( Param[0]->Val->Pointer, WM_SYSCOMMAND, SC_MAXIMIZE, (LPARAM)NULL );
}
void WinWindowMinimize(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	SendMessage( Param[0]->Val->Pointer, WM_SYSCOMMAND, SC_MINIMIZE, (LPARAM)NULL );
}

BOOL CALLBACK EnumWndProcMinimizeAll(HWND hFen,LPARAM pLstWnd) {
	if( IsWindowVisible( hFen ) )
	if( !IsIconic( hFen ) )
		SendMessage( hFen, WM_SYSCOMMAND, SC_MINIMIZE, (LPARAM)NULL );	
	return TRUE;
	}
void WinMinimizeAllWindows(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	EnumWindows(EnumWndProcMinimizeAll,(LPARAM)NULL) ;
}
BOOL CALLBACK EnumWndProcMaximizeAll(HWND hFen,LPARAM pLstWnd) {
	if( IsWindowVisible( hFen ) && IsWindow( hFen ) )
		ShowWindow( hFen, SW_RESTORE );
	return TRUE;
	}
void WinMaximizeAllWindows(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	EnumWindows(EnumWndProcMaximizeAll,(LPARAM)NULL) ;
}

void WinSendKeyboard(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	RunKeyboard( Param[0]->Val->Pointer ) ;
}

void WinSetCursorPos(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = SetCursorPos( Param[0]->Val->Integer, Param[1]->Val->Integer ) ;
}
void WinMouseLeftClick(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
}
void WinMouseRightClick(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	mouse_event(MOUSEEVENTF_RIGHTDOWN, 0, 0, 0, 0);
	mouse_event(MOUSEEVENTF_RIGHTUP, 0, 0, 0, 0);
}

void WinMouseDoubleClick(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
	mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, 0);
	mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, 0);
	ShowCursor( TRUE ) ;
}
void WinShowCursor(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = ShowCursor( Param[0]->Val->Integer ) ;
}


/* ---- common system / timing / process utilities ---- */
void WinBeep(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = Beep( Param[0]->Val->Integer, Param[1]->Val->Integer );
}
void WinGetTickCount(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)GetTickCount();
}
void WinGetLastError(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)GetLastError();
}
void WinExitProcess(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ExitProcess( (UINT)Param[0]->Val->Integer );
}
void WinGetCurrentProcessId(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)GetCurrentProcessId();
}
void WinGetCurrentThreadId(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)GetCurrentThreadId();
}
void WinGetEnvironmentVariable(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	/* GetEnvironmentVariableA(name, buf, size) -> chars copied (0 = not found) */
	ReturnValue->Val->Integer = (int)GetEnvironmentVariableA(
		Param[0]->Val->Pointer, Param[1]->Val->Pointer, (DWORD)Param[2]->Val->Integer );
}
void WinSetEnvironmentVariable(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = SetEnvironmentVariableA(
		Param[0]->Val->Pointer, Param[1]->Val->Pointer ) ? 1 : 0;
}
void WinGetTempPath(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)GetTempPathA(
		(DWORD)Param[0]->Val->Integer, Param[1]->Val->Pointer );
}
void WinGetModuleFileName(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)GetModuleFileNameA(
		NULL, Param[0]->Val->Pointer, (DWORD)Param[1]->Val->Integer );
}
void WinCopyFile(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = CopyFileA(
		Param[0]->Val->Pointer, Param[1]->Val->Pointer, Param[2]->Val->Integer ) ? 1 : 0;
}
void WinDeleteFile(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = DeleteFileA( Param[0]->Val->Pointer ) ? 1 : 0;
}
void WinMoveFile(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = MoveFileA(
		Param[0]->Val->Pointer, Param[1]->Val->Pointer ) ? 1 : 0;
}
void WinCreateDirectory(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = CreateDirectoryA( Param[0]->Val->Pointer, NULL ) ? 1 : 0;
}
void WinRemoveDirectory(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = RemoveDirectoryA( Param[0]->Val->Pointer ) ? 1 : 0;
}
void WinGetComputerName(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	DWORD n = (DWORD)Param[1]->Val->Integer;
	ReturnValue->Val->Integer = GetComputerNameA( Param[0]->Val->Pointer, &n ) ? (int)n : 0;
}
void WinGetUserName(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	DWORD n = (DWORD)Param[1]->Val->Integer;
	ReturnValue->Val->Integer = GetUserNameA( Param[0]->Val->Pointer, &n ) ? (int)n : 0;
}
void WinShellExecute(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	/* ShellExecuteA(hwnd, op, file, params, dir, show) - returns >32 on success as int */
	HINSTANCE h = ShellExecuteA(
		NULL,
		Param[0]->Val->Pointer,   /* lpOperation e.g. "open" */
		Param[1]->Val->Pointer,   /* lpFile */
		Param[2]->Val->Pointer,   /* lpParameters */
		Param[3]->Val->Pointer,   /* lpDirectory */
		Param[4]->Val->Integer ); /* nShowCmd */
	ReturnValue->Val->Integer = (int)(INT_PTR)h;
}
void WinWinExec(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = (int)WinExec( Param[0]->Val->Pointer, (UINT)Param[1]->Val->Integer );
}
void WinIsWindow(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = IsWindow( Param[0]->Val->Pointer ) ? 1 : 0;
}
void WinIsWindowVisible(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	ReturnValue->Val->Integer = IsWindowVisible( Param[0]->Val->Pointer ) ? 1 : 0;
}
void WinGetCursorPos(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	POINT pt;
	GetCursorPos(&pt);
	/* write x,y into two int pointers if provided: GetCursorPos(int *x, int *y) */
	if (Param[0]->Val->Pointer) *(int *)Param[0]->Val->Pointer = pt.x;
	if (Param[1]->Val->Pointer) *(int *)Param[1]->Val->Pointer = pt.y;
	ReturnValue->Val->Integer = 1;
}
void WinMessageBox(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs) {
	/* MessageBox(hwnd, text, caption, type) - full version */
	ReturnValue->Val->Integer = MessageBoxA(
		Param[0]->Val->Pointer,
		Param[1]->Val->Pointer,
		Param[2]->Val->Pointer,
		(UINT)Param[3]->Val->Integer );
}

/* ---- Console mode (ANSI VT). Fail soft when embedded / no console. ---- */

void WinGetStdHandle(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* HANDLE GetStdHandle(int nStdHandle);  nStdHandle is STD_* as int */
    HANDLE h = GetStdHandle((DWORD)Param[0]->Val->Integer);
    if (h == INVALID_HANDLE_VALUE || h == NULL)
        ReturnValue->Val->Pointer = NULL;
    else
        ReturnValue->Val->Pointer = (void *)h;
}

void WinGetConsoleMode(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* int GetConsoleMode(HANDLE h, unsigned int *mode); returns 0 on failure */
    HANDLE h = (HANDLE)Param[0]->Val->Pointer;
    DWORD mode = 0;
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    if (!GetConsoleMode(h, &mode)) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    if (Param[1]->Val->Pointer)
        *(int *)Param[1]->Val->Pointer = (int)mode;
    ReturnValue->Val->Integer = 1;
}

void WinSetConsoleMode(struct ParseState *Parser, struct Value *ReturnValue, struct Value **Param, int NumArgs)
{
    /* int SetConsoleMode(HANDLE h, unsigned int mode); returns 0 on failure */
    HANDLE h = (HANDLE)Param[0]->Val->Pointer;
    DWORD mode = (DWORD)Param[1]->Val->Integer;
    if (h == NULL || h == INVALID_HANDLE_VALUE) {
        ReturnValue->Val->Integer = 0;
        return;
    }
    ReturnValue->Val->Integer = SetConsoleMode(h, mode) ? 1 : 0;
}


/* Time/performance APIs shared with libWINAPI on Linux and native Win32 on Windows. */
void WinGetSystemTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; GetSystemTime((LPSYSTEMTIME)a[0]->Val->Pointer); }
void WinGetLocalTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; GetLocalTime((LPSYSTEMTIME)a[0]->Val->Pointer); }
void WinSetLocalTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetLocalTime((const SYSTEMTIME *)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinGetSystemTimeAsFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; GetSystemTimeAsFileTime((LPFILETIME)a[0]->Val->Pointer); }
void WinSystemTimeToFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SystemTimeToFileTime((const SYSTEMTIME *)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
void WinFileTimeToSystemTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FileTimeToSystemTime((const FILETIME *)a[0]->Val->Pointer, (LPSYSTEMTIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
void WinCompareFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)CompareFileTime((const FILETIME *)a[0]->Val->Pointer, (const FILETIME *)a[1]->Val->Pointer); }
void WinFileTimeToLocalFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FileTimeToLocalFileTime((const FILETIME *)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
void WinLocalFileTimeToFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = LocalFileTimeToFileTime((const FILETIME *)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
void WinGetSystemTimes(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetSystemTimes((LPFILETIME)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer, (LPFILETIME)a[2]->Val->Pointer) ? TRUE : FALSE; }
void WinQueryPerformanceCounter(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = QueryPerformanceCounter((LARGE_INTEGER *)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinQueryPerformanceFrequency(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = QueryPerformanceFrequency((LARGE_INTEGER *)a[0]->Val->Pointer) ? TRUE : FALSE; }

/* Core Win32 file API bindings: the same interpreted declarations are
 * exposed on Windows and Linux; this branch forwards to the native OS. */
void WinSetLastError(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; SetLastError((DWORD)a[0]->Val->Integer); }
void WinCreateFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = CreateFile((LPCTSTR)a[0]->Val->Pointer, (DWORD)a[1]->Val->Integer, (DWORD)a[2]->Val->Integer, (LPSECURITY_ATTRIBUTES)a[3]->Val->Pointer, (DWORD)a[4]->Val->Integer, (DWORD)a[5]->Val->Integer, (HANDLE)a[6]->Val->Pointer); }
void WinReadFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = ReadFile((HANDLE)a[0]->Val->Pointer, a[1]->Val->Pointer, (DWORD)a[2]->Val->Integer, (LPDWORD)a[3]->Val->Pointer, (LPOVERLAPPED)a[4]->Val->Pointer) ? TRUE : FALSE; }
void WinWriteFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = WriteFile((HANDLE)a[0]->Val->Pointer, a[1]->Val->Pointer, (DWORD)a[2]->Val->Integer, (LPDWORD)a[3]->Val->Pointer, (LPOVERLAPPED)a[4]->Val->Pointer) ? TRUE : FALSE; }
void WinCloseHandle(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = CloseHandle((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinSetFilePointer(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)SetFilePointer((HANDLE)a[0]->Val->Pointer, (LONG)a[1]->Val->Integer, (PLONG)a[2]->Val->Pointer, (DWORD)a[3]->Val->Integer); }
void WinGetFileSize(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetFileSize((HANDLE)a[0]->Val->Pointer, (LPDWORD)a[1]->Val->Pointer); }
void WinGetFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetFileTime((HANDLE)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer, (LPFILETIME)a[2]->Val->Pointer, (LPFILETIME)a[3]->Val->Pointer) ? TRUE : FALSE; }
void WinGetFileSizeEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetFileSizeEx((HANDLE)a[0]->Val->Pointer, (PLARGE_INTEGER)a[1]->Val->Pointer) ? TRUE : FALSE; }
void WinGetCurrentDirectory(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetCurrentDirectory((DWORD)a[0]->Val->Integer, (LPTSTR)a[1]->Val->Pointer); }
void WinSetCurrentDirectory(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetCurrentDirectory((LPCTSTR)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinFindFirstFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = FindFirstFile((LPCTSTR)a[0]->Val->Pointer, (LPWIN32_FIND_DATA)a[1]->Val->Pointer); }
void WinFindNextFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FindNextFile((HANDLE)a[0]->Val->Pointer, (LPWIN32_FIND_DATA)a[1]->Val->Pointer) ? TRUE : FALSE; }
void WinFindClose(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FindClose((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }

void WinFormatMessage(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)FormatMessageA((DWORD)a[0]->Val->Integer, (LPCVOID)a[1]->Val->Pointer, (DWORD)a[2]->Val->Integer, (DWORD)a[3]->Val->Integer, (LPSTR)a[4]->Val->Pointer, (DWORD)a[5]->Val->Integer, NULL); }
void WinGlobalMemoryStatusEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GlobalMemoryStatusEx((LPMEMORYSTATUSEX)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinCreateEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = CreateEventA((LPSECURITY_ATTRIBUTES)a[0]->Val->Pointer, (BOOL)a[1]->Val->Integer, (BOOL)a[2]->Val->Integer, (LPCSTR)a[3]->Val->Pointer); }
void WinOpenEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = OpenEventA((DWORD)a[0]->Val->Integer, (BOOL)a[1]->Val->Integer, (LPCSTR)a[2]->Val->Pointer); }
void WinSetEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetEvent((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinResetEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = ResetEvent((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinCreateMutex(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = CreateMutexA((LPSECURITY_ATTRIBUTES)a[0]->Val->Pointer, (BOOL)a[1]->Val->Integer, (LPCSTR)a[2]->Val->Pointer); }
void WinReleaseMutex(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = ReleaseMutex((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
void WinWaitForSingleObject(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    int gil;
    (void)p; (void)n;
    gil = BabycGuestGIL_UnlockIfHeld();
    r->Val->Integer = (long)WaitForSingleObject((HANDLE)a[0]->Val->Pointer, (DWORD)a[1]->Val->Integer);
    BabycGuestGIL_RelockIf(gil);
}
void WinCreateFileMapping(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = CreateFileMappingA((HANDLE)a[0]->Val->Pointer, (LPSECURITY_ATTRIBUTES)a[1]->Val->Pointer, (DWORD)a[2]->Val->Integer, (DWORD)a[3]->Val->Integer, (DWORD)a[4]->Val->Integer, (LPCSTR)a[5]->Val->Pointer); }
void WinOpenFileMapping(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = OpenFileMappingA((DWORD)a[0]->Val->Integer, (BOOL)a[1]->Val->Integer, (LPCSTR)a[2]->Val->Pointer); }
void WinMapViewOfFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = MapViewOfFile((HANDLE)a[0]->Val->Pointer, (DWORD)a[1]->Val->Integer, (DWORD)a[2]->Val->Integer, (DWORD)a[3]->Val->Integer, (SIZE_T)a[4]->Val->Integer); }
void WinUnmapViewOfFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = UnmapViewOfFile((LPCVOID)a[0]->Val->Pointer) ? TRUE : FALSE; }

void WinGetDiskFreeSpaceEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetDiskFreeSpaceExA((LPCSTR)a[0]->Val->Pointer, (PULARGE_INTEGER)a[1]->Val->Pointer, (PULARGE_INTEGER)a[2]->Val->Pointer, (PULARGE_INTEGER)a[3]->Val->Pointer) ? TRUE : FALSE; }
void WinGetLogicalDriveStrings(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetLogicalDriveStringsA((DWORD)a[0]->Val->Integer, (LPSTR)a[1]->Val->Pointer); }
/* all various.h functions */
struct LibraryFunction WinFunctions[] =
{
	{ WinMsgBox,			"void MsgBox( LPCTSTR title, LPCTSTR msg ) ;" },
	{ WinGetForegroundWindow,	"HWND GetForegroundWindow( void ) ;" },
	{ WinGetWindowText,		"void GetWindowText( HWND hWnd, LPCTSTR lpString, int nMaxCount ) ;" },
	{ WinShowWindow,		"BOOL ShowWindow( HWND hWnd, int nCmdShow ) ; " },
	{ WinFindWindow,		"HWND FindWindow( LPCTSTR lpClassName, LPCTSTR lpWindowName ) ;" },
	{ WinActiveWindow,		"void ActiveWindow( HWND hwnd ) ;" },
	{ WinActiveWindowWithPattern,	"int ActiveWindowWithPattern( LPCTSTR lpWindowName ) ;" },
	{ WinCloseWindow,		"void CloseWindow( HWND hwnd ) ;" },
	{ WinBringWindowToTop,		"BOOL BringWindowToTop( HWND hwnd ) ;" },
	{ WinSetWindowPos,		"BOOL SetWindowPos( HWND hWnd, void* hWndInsertAfter, int X, int Y, int cx, int cy, UINT uFlags ) ;" },
	{ WinSendMessage,		"LRESULT SendMessage( HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam ) ;" },
	{ WinSleep,			"void Sleep( long time ) ;" },
	{ WinWindowMaximize,		"void MaximizeWindow( HWND hwnd ) ;" },
	{ WinWindowMinimize,		"void MinimizeWindow( HWND hwnd ) ;" },
	{ WinMaximizeAllWindows,	"void MaximizeAllWindows( void ) ;" },
	{ WinMinimizeAllWindows,	"void MinimizeAllWindows( void ) ;" },
	{ WinSendKeyboard,		"void SendKeyboard( char * keys ) ;" },
	{ WinSetCursorPos,		"BOOL SetCursorPos( int X, int Y ) ;" },
	{ WinMouseLeftClick,		"void MouseLeftClick( void ) ;" },
	{ WinMouseRightClick,		"void MouseRightClick( void ) ;" },
	{ WinMouseDoubleClick,		"void MouseDoubleClick( void ) ;" },
	{ WinShowCursor,		"int ShowCursor( BOOL bShow ) ;" },
	{ WinBeep,			"int Beep( int freq, int duration ) ;" },
	{ WinGetTickCount,		"int GetTickCount( void ) ;" },
	{ WinGetLastError,		"int GetLastError( void ) ;" },
	{ WinExitProcess,		"void ExitProcess( int exitCode ) ;" },
	{ WinGetCurrentProcessId,	"int GetCurrentProcessId( void ) ;" },
	{ WinGetCurrentThreadId,	"int GetCurrentThreadId( void ) ;" },
	{ WinGetEnvironmentVariable,	"int GetEnvironmentVariable( char *name, char *buf, int size ) ;" },
	{ WinSetEnvironmentVariable,	"int SetEnvironmentVariable( char *name, char *value ) ;" },
	{ WinGetTempPath,		"int GetTempPath( int bufLen, char *buf ) ;" },
	{ WinGetModuleFileName,		"int GetModuleFileName( char *buf, int size ) ;" },
	{ WinCopyFile,			"int CopyFile( char *src, char *dst, int failIfExists ) ;" },
	{ WinDeleteFile,		"int DeleteFile( char *path ) ;" },
	{ WinMoveFile,			"int MoveFile( char *src, char *dst ) ;" },
	{ WinCreateDirectory,		"int CreateDirectory( char *path ) ;" },
	{ WinRemoveDirectory,		"int RemoveDirectory( char *path ) ;" },
	{ WinGetComputerName,		"int GetComputerName( char *buf, int size ) ;" },
	{ WinGetUserName,		"int GetUserName( char *buf, int size ) ;" },
	{ WinShellExecute,		"int ShellExecute( char *op, char *file, char *params, char *dir, int show ) ;" },
	{ WinWinExec,			"int WinExec( char *cmd, int show ) ;" },
	{ WinIsWindow,			"int IsWindow( HWND hwnd ) ;" },
	{ WinIsWindowVisible,		"int IsWindowVisible( HWND hwnd ) ;" },
	{ WinGetCursorPos,		"int GetCursorPos( int *x, int *y ) ;" },
	{ WinMessageBox,		"int MessageBox( HWND hwnd, char *text, char *caption, int type ) ;" },
	{ WinGetStdHandle,		"HANDLE GetStdHandle( int nStdHandle ) ;" },
	{ WinGetConsoleMode,		"int GetConsoleMode( HANDLE hConsole, DWORD *lpMode ) ;" },
	{ WinSetConsoleMode,		"int SetConsoleMode( HANDLE hConsole, DWORD dwMode ) ;" },
    { WinSetLastError, "void SetLastError(DWORD);" },
    { WinGetSystemTime, "void GetSystemTime(SYSTEMTIME *);" },
    { WinGetLocalTime, "void GetLocalTime(SYSTEMTIME *);" },
    { WinSetLocalTime, "BOOL SetLocalTime(const SYSTEMTIME *);" },
    { WinGetSystemTimeAsFileTime, "void GetSystemTimeAsFileTime(FILETIME *);" },
    { WinSystemTimeToFileTime, "BOOL SystemTimeToFileTime(const SYSTEMTIME *, FILETIME *);" },
    { WinFileTimeToSystemTime, "BOOL FileTimeToSystemTime(const FILETIME *, SYSTEMTIME *);" },
    { WinCompareFileTime, "LONG CompareFileTime(const FILETIME *, const FILETIME *);" },
    { WinFileTimeToLocalFileTime, "BOOL FileTimeToLocalFileTime(const FILETIME *, FILETIME *);" },
    { WinLocalFileTimeToFileTime, "BOOL LocalFileTimeToFileTime(const FILETIME *, FILETIME *);" },
    { WinGetSystemTimes, "BOOL GetSystemTimes(FILETIME *, FILETIME *, FILETIME *);" },
    { WinQueryPerformanceCounter, "BOOL QueryPerformanceCounter(LARGE_INTEGER *);" },
    { WinQueryPerformanceFrequency, "BOOL QueryPerformanceFrequency(LARGE_INTEGER *);" },
    { WinCreateFile, "HANDLE CreateFile(LPCTSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);" },
    { WinReadFile, "BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, void *);" },
    { WinWriteFile, "BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, void *);" },
    { WinCloseHandle, "BOOL CloseHandle(HANDLE);" },
    { WinSetFilePointer, "DWORD SetFilePointer(HANDLE, LONG, PLONG, DWORD);" },
    { WinGetFileSize, "DWORD GetFileSize(HANDLE, LPDWORD);" },
    { WinGetFileSizeEx, "BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *);" },
    { WinGetFileTime, "BOOL GetFileTime(HANDLE, FILETIME *, FILETIME *, FILETIME *);" },
    { WinGetCurrentDirectory, "DWORD GetCurrentDirectory(DWORD, LPTSTR);" },
    { WinSetCurrentDirectory, "BOOL SetCurrentDirectory(LPCTSTR);" },
    { WinFindFirstFile, "HANDLE FindFirstFile(LPCTSTR, WIN32_FIND_DATA *);" },
    { WinFindNextFile, "BOOL FindNextFile(HANDLE, WIN32_FIND_DATA *);" },
    { WinFindClose, "BOOL FindClose(HANDLE);" },
    { WinFormatMessage, "DWORD FormatMessage(DWORD, LPCVOID, DWORD, DWORD, LPTSTR, DWORD, void *);" },
    { WinGlobalMemoryStatusEx, "BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *);" },
    { WinCreateEvent, "HANDLE CreateEvent(void *, BOOL, BOOL, LPCTSTR);" },
    { WinOpenEvent, "HANDLE OpenEvent(DWORD, BOOL, LPCTSTR);" },
    { WinSetEvent, "BOOL SetEvent(HANDLE);" },
    { WinResetEvent, "BOOL ResetEvent(HANDLE);" },
    { WinCreateMutex, "HANDLE CreateMutex(void *, BOOL, LPCTSTR);" },
    { WinReleaseMutex, "BOOL ReleaseMutex(HANDLE);" },
    { WinWaitForSingleObject, "DWORD WaitForSingleObject(HANDLE, DWORD);" },
    { WinCreateFileMapping, "HANDLE CreateFileMapping(HANDLE, void *, DWORD, DWORD, DWORD, LPCTSTR);" },
    { WinOpenFileMapping, "HANDLE OpenFileMapping(DWORD, BOOL, LPCTSTR);" },
    { WinMapViewOfFile, "LPVOID MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, unsigned int);" },
    { WinUnmapViewOfFile, "BOOL UnmapViewOfFile(LPCVOID);" },
    { WinGetDiskFreeSpaceEx, "BOOL GetDiskFreeSpaceEx(LPCTSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);" },
    { WinGetLogicalDriveStrings, "DWORD GetLogicalDriveStrings(DWORD, LPTSTR);" },
	{ NULL,			NULL }
};

/* creates various system-dependent definitions */
void WinSetupFunc(Picoc *pc)
{
	/* idempotent: only windows.h registers this setup */
    char* guard = TableStrRegister(pc, "__win_api_loaded");
    if (VariableDefined(pc, guard))
        return;
    VariableDefine(pc, NULL, guard, NULL, &pc->VoidType, FALSE);

	/* inject typedefs once */
	if (WinDefs && WinDefs[0])
		PicocParse(pc, "windows.h", WinDefs, strlen(WinDefs), TRUE, TRUE, FALSE, FALSE);
	PicocParse(pc, "winerror.h", WinErrorDefs, strlen(WinErrorDefs), TRUE, TRUE, FALSE, FALSE);

	/* register API functions once */
	LibraryAdd(pc, &pc->GlobalTable, "windows.h", &WinFunctions[0]);

	/* WinDefs already did: typedef struct __HWNDStruct HWND
	 * Set the opaque size (native HWND) without re-creating the type. */
	{
		struct ValueType *HwndTyp = TypeGetMatching(pc, NULL, &pc->UberType, TypeStruct, 0,
			TableStrRegister(pc, "__HWNDStruct"), TRUE);
		if (HwndTyp != NULL)
			HwndTyp->Sizeof = (int)sizeof(HWND);
	}
	
	/* define definitions */
	VariableDefinePlatformVar(pc, NULL, "SW_FORCEMINIMIZE", &pc->IntType, (union AnyValue *)&S_SW_FORCEMINIMIZE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_HIDE", &pc->IntType, (union AnyValue *)&S_SW_HIDE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_MAXIMIZE", &pc->IntType, (union AnyValue *)&S_SW_MAXIMIZE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_MINIMIZE", &pc->IntType, (union AnyValue *)&S_SW_MINIMIZE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_RESTORE", &pc->IntType, (union AnyValue *)&S_SW_RESTORE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOW", &pc->IntType, (union AnyValue *)&S_SW_SHOW, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWDEFAULT", &pc->IntType, (union AnyValue *)&S_SW_SHOWDEFAULT, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWMAXIMIZED", &pc->IntType, (union AnyValue *)&S_SW_SHOWMAXIMIZED, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWMINIMIZED", &pc->IntType, (union AnyValue *)&S_SW_SHOWMINIMIZED, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWMINNOACTIVE", &pc->IntType, (union AnyValue *)&S_SW_SHOWMINNOACTIVE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWNA", &pc->IntType, (union AnyValue *)&S_SW_SHOWNA, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWNOACTIVATE", &pc->IntType, (union AnyValue *)&S_SW_SHOWNOACTIVATE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SW_SHOWNORMAL", &pc->IntType, (union AnyValue *)&S_SW_SHOWNORMAL, FALSE);
	
	VariableDefinePlatformVar(pc, NULL, "HWND_BOTTOM", &pc->IntType, (union AnyValue *)&S_HWND_BOTTOM, FALSE);
	VariableDefinePlatformVar(pc, NULL, "HWND_NOTOPMOST", &pc->IntType, (union AnyValue *)&S_HWND_NOTOPMOST, FALSE);
	VariableDefinePlatformVar(pc, NULL, "HWND_TOP", &pc->IntType, (union AnyValue *)&S_HWND_TOP, FALSE);
	VariableDefinePlatformVar(pc, NULL, "HWND_TOPMOST", &pc->IntType, (union AnyValue *)&S_HWND_TOPMOST, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_ASYNCWINDOWPOS", &pc->IntType, (union AnyValue *)&S_SWP_ASYNCWINDOWPOS, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_DEFERERASE", &pc->IntType, (union AnyValue *)&S_SWP_DEFERERASE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_DRAWFRAME", &pc->IntType, (union AnyValue *)&S_SWP_DRAWFRAME, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_FRAMECHANGED", &pc->IntType, (union AnyValue *)&S_SWP_FRAMECHANGED, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_HIDEWINDOW", &pc->IntType, (union AnyValue *)&S_SWP_HIDEWINDOW, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOACTIVATE", &pc->IntType, (union AnyValue *)&S_SWP_NOACTIVATE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOCOPYBITS", &pc->IntType, (union AnyValue *)&S_SWP_NOCOPYBITS, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOMOVE", &pc->IntType, (union AnyValue *)&S_SWP_NOMOVE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOOWNERZORDER", &pc->IntType, (union AnyValue *)&S_SWP_NOOWNERZORDER, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOREDRAW", &pc->IntType, (union AnyValue *)&S_SWP_NOREDRAW, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOREPOSITION", &pc->IntType, (union AnyValue *)&S_SWP_NOREPOSITION, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOSENDCHANGING", &pc->IntType, (union AnyValue *)&S_SWP_NOSENDCHANGING, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOSIZE", &pc->IntType, (union AnyValue *)&S_SWP_NOSIZE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_NOZORDER", &pc->IntType, (union AnyValue *)&S_SWP_NOZORDER, FALSE);
	VariableDefinePlatformVar(pc, NULL, "SWP_SHOWWINDOW", &pc->IntType, (union AnyValue *)&S_SWP_SHOWWINDOW, FALSE);
	
	VariableDefinePlatformVar(pc, NULL, "S_WM_COMMAND", &pc->IntType, (union AnyValue *)&S_WM_COMMAND, FALSE);
	VariableDefinePlatformVar(pc, NULL, "S_WM_SYSCOMMAND", &pc->IntType, (union AnyValue *)&S_WM_SYSCOMMAND, FALSE);
	VariableDefinePlatformVar(pc, NULL, "S_SC_MAXIMIZE", &pc->IntType, (union AnyValue *)&S_SC_MAXIMIZE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "S_SC_MINIMIZE", &pc->IntType, (union AnyValue *)&S_SC_MINIMIZE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "S_SC_CLOSE", &pc->IntType, (union AnyValue *)&S_SC_CLOSE, FALSE);

	VariableDefinePlatformVar(pc, NULL, "MB_OK", &pc->IntType, (union AnyValue *)&S_MB_OK, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_OKCANCEL", &pc->IntType, (union AnyValue *)&S_MB_OKCANCEL, FALSE);

	VariableDefinePlatformVar(pc, NULL, "STD_INPUT_HANDLE", &pc->IntType, (union AnyValue *)&S_STD_INPUT_HANDLE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "STD_OUTPUT_HANDLE", &pc->IntType, (union AnyValue *)&S_STD_OUTPUT_HANDLE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "STD_ERROR_HANDLE", &pc->IntType, (union AnyValue *)&S_STD_ERROR_HANDLE, FALSE);
	VariableDefinePlatformVar(pc, NULL, "ENABLE_VIRTUAL_TERMINAL_PROCESSING", &pc->IntType, (union AnyValue *)&S_ENABLE_VIRTUAL_TERMINAL_PROCESSING, FALSE);
	VariableDefinePlatformVar(pc, NULL, "DISABLE_NEWLINE_AUTO_RETURN", &pc->IntType, (union AnyValue *)&S_DISABLE_NEWLINE_AUTO_RETURN, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_YESNO", &pc->IntType, (union AnyValue *)&S_MB_YESNO, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_YESNOCANCEL", &pc->IntType, (union AnyValue *)&S_MB_YESNOCANCEL, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_ICONERROR", &pc->IntType, (union AnyValue *)&S_MB_ICONERROR, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_ICONWARNING", &pc->IntType, (union AnyValue *)&S_MB_ICONWARNING, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_ICONINFORMATION", &pc->IntType, (union AnyValue *)&S_MB_ICONINFORMATION, FALSE);
	VariableDefinePlatformVar(pc, NULL, "MB_ICONQUESTION", &pc->IntType, (union AnyValue *)&S_MB_ICONQUESTION, FALSE);
	VariableDefinePlatformVar(pc, NULL, "IDOK", &pc->IntType, (union AnyValue *)&S_IDOK, FALSE);
	VariableDefinePlatformVar(pc, NULL, "IDCANCEL", &pc->IntType, (union AnyValue *)&S_IDCANCEL, FALSE);
	VariableDefinePlatformVar(pc, NULL, "IDYES", &pc->IntType, (union AnyValue *)&S_IDYES, FALSE);
	VariableDefinePlatformVar(pc, NULL, "IDNO", &pc->IntType, (union AnyValue *)&S_IDNO, FALSE);
}


/*
HWND hwnd; hwnd=GetForegroundWindow() ;
ActiveWindowWithPattern( "*Bloc*" ); hwnd=GetForegroundWindow() ; SendKeyboard("Ceci est un test !!!" ); 
SetWindowPos(hwnd,HWND_TOP,10,10,0,0,SWP_NOSIZE|SWP_NOZORDER);
char buffer[1024];
GetWindowText(hwnd, buffer, 1024 );
GetWindowText(hwnd, buffer, 1024 );
printf("%s\n",buffer);
hwnd=FindWindow(NULL,"Sans titre - Bloc-notes");
WindowMaximize(hwnd);
WindowMinimize(hwnd);
*/

#else
/* Linux host: interpreted Win32 APIs are backed by libWINAPI.  Keep this
 * translation unit independent of the host's windows.h and shell APIs. */
#include "../interpreter.h"
#include "../picoc.h"
#include "../winapi/include/windows.h"
#include "../platform/guest_thread.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pwd.h>

static char baby_std_input_token;
static char baby_std_output_token;
static char baby_std_error_token;
static int S_STD_INPUT_HANDLE = -10;
static int S_STD_OUTPUT_HANDLE = -11;
static int S_STD_ERROR_HANDLE = -12;
#define BABY_STD_INPUT  ((HANDLE)&baby_std_input_token)
#define BABY_STD_OUTPUT ((HANDLE)&baby_std_output_token)
#define BABY_STD_ERROR  ((HANDLE)&baby_std_error_token)

const char WinDefs[] =
    "typedef void *HANDLE;\n"
    "typedef unsigned int DWORD;\n"
    "typedef unsigned int UINT;\n"
    "typedef int BOOL;\n"
    "typedef int LONG;\n"
    "typedef unsigned short WORD;\n"
    "typedef char *LPCTSTR;\n"
    "typedef char *LPTSTR;\n"
    "typedef void *LPVOID;\n"
    "typedef const void *LPCVOID;\n"
    "typedef DWORD *LPDWORD;\n"
    "typedef int *PLONG;\n"
    "typedef void *HWND;\n"
    "typedef unsigned int WPARAM;\n"
    "typedef long LPARAM;\n"
    "typedef long LRESULT;\n"
    "typedef struct _FILETIME { DWORD dwLowDateTime; DWORD dwHighDateTime; } FILETIME;\n"
    "typedef struct _SYSTEMTIME { WORD wYear; WORD wMonth; WORD wDayOfWeek; WORD wDay; WORD wHour; WORD wMinute; WORD wSecond; WORD wMilliseconds; } SYSTEMTIME;\n"
    "typedef struct { char __cs[64]; } CRITICAL_SECTION;\n"
    "#define STILL_ACTIVE 259\n"
    "typedef struct _LARGE_INTEGER { long long QuadPart; } LARGE_INTEGER;\n"
    "typedef struct _ULARGE_INTEGER { unsigned long long QuadPart; } ULARGE_INTEGER;\n"
    "typedef ULARGE_INTEGER *PULARGE_INTEGER;\n"
    "typedef unsigned long long DWORDLONG;\n"
    "typedef struct _MEMORYSTATUSEX { DWORD dwLength; DWORD dwMemoryLoad; DWORDLONG ullTotalPhys; DWORDLONG ullAvailPhys; DWORDLONG ullTotalPageFile; DWORDLONG ullAvailPageFile; DWORDLONG ullTotalVirtual; DWORDLONG ullAvailVirtual; DWORDLONG ullAvailExtendedVirtual; } MEMORYSTATUSEX;\n"
    "typedef MEMORYSTATUSEX *LPMEMORYSTATUSEX;\n"
    "typedef struct _WIN32_FIND_DATA { DWORD dwFileAttributes; FILETIME ftCreationTime; FILETIME ftLastAccessTime; FILETIME ftLastWriteTime; DWORD nFileSizeHigh; DWORD nFileSizeLow; DWORD dwReserved0; DWORD dwReserved1; char cFileName[260]; char cAlternateFileName[14]; } WIN32_FIND_DATA;\n"
    "#define TRUE 1\n#define FALSE 0\n"
    "#define INVALID_HANDLE_VALUE ((HANDLE)-1)\n"
    "#define GENERIC_READ 0x80000000u\n#define GENERIC_WRITE 0x40000000u\n"
    "#define FILE_SHARE_READ 1u\n#define FILE_SHARE_WRITE 2u\n#define FILE_SHARE_DELETE 4u\n"
    "#define CREATE_NEW 1u\n#define CREATE_ALWAYS 2u\n#define OPEN_EXISTING 3u\n#define OPEN_ALWAYS 4u\n#define TRUNCATE_EXISTING 5u\n"
    "#define FILE_ATTRIBUTE_NORMAL 0x80u\n"
    "#define FILE_BEGIN 0u\n#define FILE_CURRENT 1u\n#define FILE_END 2u\n"
    "#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200u\n#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000u\n"
    "#define INFINITE 0xffffffffu\n#define WAIT_OBJECT_0 0u\n#define WAIT_TIMEOUT 0x102u\n#define WAIT_FAILED 0xffffffffu\n"
    "#define EVENT_MODIFY_STATE 2u\n#define EVENT_ALL_ACCESS 0x1f0003u\n#define MUTEX_MODIFY_STATE 1u\n#define MUTEX_ALL_ACCESS 0x1f0001u\n"
    "#define PAGE_READONLY 2u\n#define PAGE_READWRITE 4u\n#define FILE_MAP_COPY 1u\n#define FILE_MAP_WRITE 2u\n#define FILE_MAP_READ 4u\n#define FILE_MAP_ALL_ACCESS 0xf001fu\n"
    "#define DRIVE_UNKNOWN 0u\n#define DRIVE_NO_ROOT_DIR 1u\n#define DRIVE_REMOVABLE 2u\n#define DRIVE_FIXED 3u\n#define DRIVE_REMOTE 4u\n#define DRIVE_CDROM 5u\n#define DRIVE_RAMDISK 6u\n"
    "#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004u\n"
    "#define DISABLE_NEWLINE_AUTO_RETURN 0x0008u\n"
    "#define MAX_PATH 260\n"
    "#define SW_HIDE 0\n#define SW_SHOWNORMAL 1\n#define SW_SHOWMINIMIZED 2\n#define SW_MAXIMIZE 3\n#define SW_SHOWNOACTIVATE 4\n#define SW_SHOW 5\n#define SW_MINIMIZE 6\n#define SW_SHOWMINNOACTIVE 7\n#define SW_SHOWNA 8\n#define SW_RESTORE 9\n#define SW_SHOWDEFAULT 10\n#define SW_FORCEMINIMIZE 11\n"
    "#define HWND_TOP 0\n#define HWND_BOTTOM 1\n#define HWND_NOTOPMOST -2\n#define HWND_TOPMOST -1\n"
    "#define SWP_NOSIZE 0x0001u\n#define SWP_NOMOVE 0x0002u\n#define SWP_NOZORDER 0x0004u\n#define SWP_NOREDRAW 0x0008u\n#define SWP_NOACTIVATE 0x0010u\n#define SWP_FRAMECHANGED 0x0020u\n#define SWP_SHOWWINDOW 0x0040u\n#define SWP_HIDEWINDOW 0x0080u\n#define SWP_NOCOPYBITS 0x0100u\n#define SWP_NOOWNERZORDER 0x0200u\n#define SWP_NOSENDCHANGING 0x0400u\n#define SWP_DRAWFRAME 0x0020u\n#define SWP_NOREPOSITION 0x0200u\n#define SWP_DEFERERASE 0x2000u\n#define SWP_ASYNCWINDOWPOS 0x4000u\n"
    "#define WM_COMMAND 0x0111u\n#define WM_SYSCOMMAND 0x0112u\n#define SC_CLOSE 0xF060u\n#define SC_MAXIMIZE 0xF030u\n#define SC_MINIMIZE 0xF020u\n"
    "#define MB_OK 0u\n#define MB_OKCANCEL 1u\n#define MB_YESNO 4u\n#define MB_YESNOCANCEL 3u\n#define MB_ICONERROR 0x10u\n#define MB_ICONWARNING 0x30u\n#define MB_ICONINFORMATION 0x40u\n#define MB_ICONQUESTION 0x20u\n"
    "#define IDOK 1\n#define IDCANCEL 2\n#define IDYES 6\n#define IDNO 7\n";

static void WinGetLastError(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Integer = (long)GetLastError(); }
static void WinSetLastError(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; SetLastError((DWORD)a[0]->Val->Integer); }
static void WinSleep(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; Sleep((DWORD)a[0]->Val->Integer); }
static void WinGetTickCount(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    struct timespec ts;
    (void)p; (void)a; (void)n;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) { r->Val->Integer = 0; return; }
    r->Val->Integer = (long)(((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u) & 0xffffffffu);
}
static void WinGetCurrentProcessId(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Integer = (long)getpid(); }
static void WinGetCurrentThreadId(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Integer = (long)GetCurrentThreadId(); }
static void WinExitProcess(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; ExitProcess((UINT)a[0]->Val->Integer); }
static void WinCreateFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n;
    r->Val->Pointer = CreateFile((LPCTSTR)a[0]->Val->Pointer,
        (DWORD)a[1]->Val->Integer, (DWORD)a[2]->Val->Integer,
        (LPSECURITY_ATTRIBUTES)a[3]->Val->Pointer,
        (DWORD)a[4]->Val->Integer, (DWORD)a[5]->Val->Integer,
        (HANDLE)a[6]->Val->Pointer);
}
static void WinReadFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    HANDLE h = (HANDLE)a[0]->Val->Pointer;
    DWORD count = (DWORD)a[2]->Val->Integer, done = 0;
    BOOL ok;
    (void)p; (void)n;
    if (h == BABY_STD_INPUT) {
        size_t got = fread(a[1]->Val->Pointer, 1, count, stdin);
        done = (DWORD)got; ok = !ferror(stdin);
    } else {
        ok = ReadFile(h, a[1]->Val->Pointer, count, &done, (LPOVERLAPPED)a[4]->Val->Pointer);
    }
    if (a[3]->Val->Pointer != NULL) *(DWORD *)a[3]->Val->Pointer = done;
    r->Val->Integer = ok ? TRUE : FALSE;
}
static void WinWriteFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    HANDLE h = (HANDLE)a[0]->Val->Pointer;
    DWORD count = (DWORD)a[2]->Val->Integer, done = 0;
    BOOL ok;
    (void)p; (void)n;
    if (h == BABY_STD_OUTPUT || h == BABY_STD_ERROR) {
        FILE *f = h == BABY_STD_OUTPUT ? stdout : stderr;
        done = (DWORD)fwrite(a[1]->Val->Pointer, 1, count, f);
        ok = done == count && fflush(f) == 0;
    } else {
        ok = WriteFile(h, a[1]->Val->Pointer, count, &done, (LPOVERLAPPED)a[4]->Val->Pointer);
    }
    if (a[3]->Val->Pointer != NULL) *(DWORD *)a[3]->Val->Pointer = done;
    r->Val->Integer = ok ? TRUE : FALSE;
}
static void WinCloseHandle(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    HANDLE h = (HANDLE)a[0]->Val->Pointer; (void)p; (void)n;
    if (h == BABY_STD_INPUT || h == BABY_STD_OUTPUT || h == BABY_STD_ERROR) {
        SetLastError(ERROR_INVALID_HANDLE); r->Val->Integer = FALSE;
    } else r->Val->Integer = CloseHandle(h) ? TRUE : FALSE;
}
static void WinSetFilePointer(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)SetFilePointer((HANDLE)a[0]->Val->Pointer, (LONG)a[1]->Val->Integer, (PLONG)a[2]->Val->Pointer, (DWORD)a[3]->Val->Integer); }
static void WinGetFileSize(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetFileSize((HANDLE)a[0]->Val->Pointer, (LPDWORD)a[1]->Val->Pointer); }
static void WinGetFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetFileTime((HANDLE)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer, (LPFILETIME)a[2]->Val->Pointer, (LPFILETIME)a[3]->Val->Pointer) ? TRUE : FALSE; }
static void WinGetFileSizeEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetFileSizeEx((HANDLE)a[0]->Val->Pointer, (PLARGE_INTEGER)a[1]->Val->Pointer) ? TRUE : FALSE; }
static void WinGetCurrentDirectory(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetCurrentDirectory((DWORD)a[0]->Val->Integer, (LPTSTR)a[1]->Val->Pointer); }
static void WinSetCurrentDirectory(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetCurrentDirectory((LPCTSTR)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinFindFirstFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = FindFirstFile((LPCTSTR)a[0]->Val->Pointer, (LPWIN32_FIND_DATA)a[1]->Val->Pointer); }
static void WinFindNextFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FindNextFile((HANDLE)a[0]->Val->Pointer, (LPWIN32_FIND_DATA)a[1]->Val->Pointer) ? TRUE : FALSE; }
static void WinFindClose(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FindClose((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinGetStdHandle(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n;
    switch ((int)a[0]->Val->Integer) {
    case -10: r->Val->Pointer = BABY_STD_INPUT; break;
    case -11: r->Val->Pointer = BABY_STD_OUTPUT; break;
    case -12: r->Val->Pointer = BABY_STD_ERROR; break;
    default: r->Val->Pointer = NULL; SetLastError(ERROR_INVALID_PARAMETER); break;
    }
}
static void WinGetConsoleMode(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Integer = FALSE; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); }
static void WinSetConsoleMode(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Integer = FALSE; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); }

static void WinGetSystemTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; GetSystemTime((LPSYSTEMTIME)a[0]->Val->Pointer); }
static void WinGetLocalTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; GetLocalTime((LPSYSTEMTIME)a[0]->Val->Pointer); }
static void WinSetLocalTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetLocalTime((const SYSTEMTIME *)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinGetSystemTimeAsFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; GetSystemTimeAsFileTime((LPFILETIME)a[0]->Val->Pointer); }
static void WinSystemTimeToFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SystemTimeToFileTime((const SYSTEMTIME *)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
static void WinFileTimeToSystemTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FileTimeToSystemTime((const FILETIME *)a[0]->Val->Pointer, (LPSYSTEMTIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
static void WinCompareFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)CompareFileTime((const FILETIME *)a[0]->Val->Pointer, (const FILETIME *)a[1]->Val->Pointer); }
static void WinFileTimeToLocalFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = FileTimeToLocalFileTime((const FILETIME *)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
static void WinLocalFileTimeToFileTime(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = LocalFileTimeToFileTime((const FILETIME *)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer) ? TRUE : FALSE; }
static void WinGetSystemTimes(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetSystemTimes((LPFILETIME)a[0]->Val->Pointer, (LPFILETIME)a[1]->Val->Pointer, (LPFILETIME)a[2]->Val->Pointer) ? TRUE : FALSE; }
static void WinQueryPerformanceCounter(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = QueryPerformanceCounter((LARGE_INTEGER *)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinQueryPerformanceFrequency(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = QueryPerformanceFrequency((LARGE_INTEGER *)a[0]->Val->Pointer) ? TRUE : FALSE; }

static DWORD baby_win_error_from_errno(int e)
{
    switch (e) {
    case 0: return ERROR_SUCCESS;
    case ENOENT: return ERROR_FILE_NOT_FOUND;
    case ENOTDIR: return ERROR_PATH_NOT_FOUND;
    case EACCES: case EPERM: case EROFS: return ERROR_ACCESS_DENIED;
    case EEXIST: return ERROR_FILE_EXISTS;
    case ENOMEM: return ERROR_NOT_ENOUGH_MEMORY;
    case ENAMETOOLONG: return ERROR_FILENAME_EXCED_RANGE;
    case EMFILE: case ENFILE: return ERROR_TOO_MANY_OPEN_FILES;
    case ENOSPC: return ERROR_DISK_FULL;
    case EINVAL: return ERROR_INVALID_PARAMETER;
    default: return ERROR_GEN_FAILURE;
    }
}
static void WinGetEnvironmentVariable(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    const char *value; char *buffer = (char *)a[1]->Val->Pointer;
    DWORD size = (DWORD)a[2]->Val->Integer; size_t need;
    (void)p; (void)n;
    if (!a[0]->Val->Pointer || (size && !buffer)) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = 0; return; }
    value = getenv((const char *)a[0]->Val->Pointer);
    if (!value) { if (buffer && size) buffer[0] = '\0'; SetLastError(ERROR_ENVVAR_NOT_FOUND); r->Val->Integer = 0; return; }
    need = strlen(value) + 1;
    if (size == 0 || need > size) { if (buffer && size) buffer[0] = '\0'; r->Val->Integer = (long)need; return; }
    memcpy(buffer, value, need); r->Val->Integer = (long)(need - 1);
}
static void WinSetEnvironmentVariable(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    const char *name = (const char *)a[0]->Val->Pointer, *value = (const char *)a[1]->Val->Pointer;
    (void)p; (void)n;
    if (!name || !*name) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = FALSE; return; }
    if ((value ? setenv(name, value, 1) : unsetenv(name)) != 0) { SetLastError(baby_win_error_from_errno(errno)); r->Val->Integer = FALSE; return; }
    r->Val->Integer = TRUE;
}
static void WinGetTempPath(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    const char *base = getenv("TMPDIR"); char *buffer = (char *)a[1]->Val->Pointer;
    size_t raw, need; DWORD cap = (DWORD)a[0]->Val->Integer;
    (void)p; (void)n;
    if (!base || !*base) base = "/tmp";
    raw = strlen(base); need = raw + ((raw == 0 || base[raw - 1] != '/') ? 1 : 0);
    if (!buffer || cap == 0 || need + 1 > cap) { if (buffer && cap) buffer[0] = '\0'; r->Val->Integer = (long)(need + 1); return; }
    memcpy(buffer, base, raw); if (need > raw) buffer[raw] = '/'; buffer[need] = '\0'; r->Val->Integer = (long)need;
}
static void WinGetModuleFileName(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    char *buffer = (char *)a[0]->Val->Pointer; size_t cap = (size_t)a[1]->Val->Integer; ssize_t got;
    (void)p; (void)n;
    if (!buffer || cap == 0) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = 0; return; }
    got = readlink("/proc/self/exe", buffer, cap - 1);
    if (got < 0) { SetLastError(baby_win_error_from_errno(errno)); buffer[0] = '\0'; r->Val->Integer = 0; return; }
    if ((size_t)got >= cap - 1) { buffer[cap - 1] = '\0'; SetLastError(ERROR_INSUFFICIENT_BUFFER); r->Val->Integer = (long)cap; return; }
    buffer[got] = '\0'; r->Val->Integer = (long)got;
}
static void WinCopyFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    const char *src = (const char *)a[0]->Val->Pointer, *dst = (const char *)a[1]->Val->Pointer;
    int fail_if_exists = (int)a[2]->Val->Integer, in = -1, out = -1, saved;
    char buf[16384]; ssize_t nr;
    (void)p; (void)n;
    if (!src || !dst) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = FALSE; return; }
    in = open(src, O_RDONLY | O_CLOEXEC); if (in < 0) goto fail;
    out = open(dst, O_WRONLY | O_CREAT | O_CLOEXEC | (fail_if_exists ? O_EXCL : O_TRUNC), 0666); if (out < 0) goto fail;
    while ((nr = read(in, buf, sizeof(buf))) != 0) {
        ssize_t off = 0;
        if (nr < 0) { if (errno == EINTR) continue; goto fail; }
        while (off < nr) { ssize_t nw = write(out, buf + off, (size_t)(nr - off)); if (nw < 0 && errno == EINTR) continue; if (nw <= 0) { if (nw == 0) errno = EIO; goto fail; } off += nw; }
    }
    if (close(in) != 0) { in = -1; goto fail; } in = -1;
    if (close(out) != 0) { out = -1; goto fail; } out = -1; r->Val->Integer = TRUE; return;
fail:
    saved = errno; if (in >= 0) close(in); if (out >= 0) close(out); if (out >= 0 && !fail_if_exists) unlink(dst);
    SetLastError(baby_win_error_from_errno(saved)); r->Val->Integer = FALSE;
}
static void WinDeleteFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; if (!a[0]->Val->Pointer) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = FALSE; return; } if (unlink((const char *)a[0]->Val->Pointer) == 0) r->Val->Integer = TRUE; else { SetLastError(baby_win_error_from_errno(errno)); r->Val->Integer = FALSE; } }
static void WinMoveFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; if (!a[0]->Val->Pointer || !a[1]->Val->Pointer) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = FALSE; return; } if (rename((const char *)a[0]->Val->Pointer, (const char *)a[1]->Val->Pointer) == 0) r->Val->Integer = TRUE; else { SetLastError(baby_win_error_from_errno(errno)); r->Val->Integer = FALSE; } }
static void WinCreateDirectory(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; if (!a[0]->Val->Pointer) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = FALSE; return; } if (win_mkdir((const char *)a[0]->Val->Pointer) == 0) r->Val->Integer = TRUE; else { SetLastError(baby_win_error_from_errno(errno)); r->Val->Integer = FALSE; } }
static void WinRemoveDirectory(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; if (!a[0]->Val->Pointer) { SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = FALSE; return; } if (win_rmdir((const char *)a[0]->Val->Pointer) == 0) r->Val->Integer = TRUE; else { SetLastError(baby_win_error_from_errno(errno)); r->Val->Integer = FALSE; } }
static void WinGetComputerName(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    char host[256]; char *buf = (char *)a[0]->Val->Pointer; size_t cap = (size_t)a[1]->Val->Integer;
    (void)p; (void)n;
    if (!buf || cap == 0 || gethostname(host, sizeof(host)) != 0) { if (buf && cap) buf[0] = '\0'; SetLastError(ERROR_INVALID_PARAMETER); r->Val->Integer = 0; return; }
    host[sizeof(host)-1] = '\0'; if (strlen(host) + 1 > cap) { buf[0] = '\0'; SetLastError(ERROR_INSUFFICIENT_BUFFER); r->Val->Integer = 0; return; }
    strcpy(buf, host); r->Val->Integer = (long)strlen(host);
}
static void WinGetUserName(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    struct passwd *pw = getpwuid(geteuid()); char *buf = (char *)a[0]->Val->Pointer; size_t cap = (size_t)a[1]->Val->Integer;
    (void)p; (void)n;
    if (!pw || !buf || cap == 0 || strlen(pw->pw_name) + 1 > cap) { if (buf && cap) buf[0] = '\0'; SetLastError(ERROR_INSUFFICIENT_BUFFER); r->Val->Integer = 0; return; }
    strcpy(buf, pw->pw_name); r->Val->Integer = (long)strlen(buf);
}
static void WinBeep(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Integer = (fputc('\a', stderr) != EOF && fflush(stderr) == 0) ? TRUE : FALSE; }
static void WinGuiUnavailable(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)a; (void)n; r->Val->Pointer = NULL; r->Val->Integer = 0; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); }
static void WinGuiGetWindowText(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)r; (void)n; if (a[1]->Val->Pointer && a[2]->Val->Integer > 0) ((char *)a[1]->Val->Pointer)[0] = '\0'; SetLastError(ERROR_CALL_NOT_IMPLEMENTED); }
static void WinFormatMessage(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)FormatMessage((DWORD)a[0]->Val->Integer, (LPCVOID)a[1]->Val->Pointer, (DWORD)a[2]->Val->Integer, (DWORD)a[3]->Val->Integer, (LPTSTR)a[4]->Val->Pointer, (DWORD)a[5]->Val->Integer, NULL); }
static void WinGlobalMemoryStatusEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GlobalMemoryStatusEx((LPMEMORYSTATUSEX)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinCreateEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = CreateEvent((LPSECURITY_ATTRIBUTES)a[0]->Val->Pointer, (BOOL)a[1]->Val->Integer, (BOOL)a[2]->Val->Integer, (LPCTSTR)a[3]->Val->Pointer); }
static void WinOpenEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = OpenEvent((DWORD)a[0]->Val->Integer, (BOOL)a[1]->Val->Integer, (LPCTSTR)a[2]->Val->Pointer); }
static void WinSetEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetEvent((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinResetEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = ResetEvent((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }

/* Guest-callback CreateThread: native OS thread + shared GIL trampoline
 * (same mechanism as BabyPOSIX pthread_create). */
struct WinGuestThreadArg {
    struct Value *func;
    void *param;
};

#ifdef WIN32
/* defined in WIN32 branch below if needed */
#else
static DWORD WINAPI WinGuestThreadStart(LPVOID p)
{
    struct WinGuestThreadArg *a = (struct WinGuestThreadArg *)p;
    DWORD code = 0;
    void *ret = NULL;
    if (a != NULL) {
        BabycGuestGIL_Lock();
        if (a->func != NULL)
            ret = BabycGuestCall(a->func, a->param);
        BabycGuestGIL_Unlock();
        code = (DWORD)(uintptr_t)ret;
        free(a);
    }
    return code;
}

static void WinCreateThread(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    struct Value *func;
    void *param;
    DWORD flags;
    DWORD *tid_out;
    struct WinGuestThreadArg *arg;
    HANDLE h;
    (void)n;

    if (p != NULL && p->pc != NULL)
        BabycGuestSetPC(p->pc);

    func = BabycGuestResolveFuncValue(a[2]);
    param = a[3]->Val->Pointer;
    flags = (DWORD)a[4]->Val->UnsignedLongInteger;
    if (flags == 0 && a[4]->Val->Integer != 0)
        flags = (DWORD)a[4]->Val->Integer;
    tid_out = (DWORD *)a[5]->Val->Pointer;

    if (func == NULL) {
        SetLastError(ERROR_INVALID_PARAMETER);
        r->Val->Pointer = NULL;
        return;
    }
    if (flags != 0) {
        /* CREATE_SUSPENDED not supported for guest callbacks */
        SetLastError(ERROR_INVALID_PARAMETER);
        r->Val->Pointer = NULL;
        return;
    }

    arg = (struct WinGuestThreadArg *)calloc(1, sizeof(*arg));
    if (arg == NULL) {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        r->Val->Pointer = NULL;
        return;
    }
    arg->func = func;
    arg->param = param;

    h = CreateThread(
        (LPSECURITY_ATTRIBUTES)a[0]->Val->Pointer,
        (SIZE_T)a[1]->Val->UnsignedLongInteger,
        WinGuestThreadStart,
        arg,
        0,
        tid_out);
    if (h == NULL) {
        free(arg);
        r->Val->Pointer = NULL;
        return;
    }
    r->Val->Pointer = h;
}
#endif

static void WinGetExitCodeThread(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n;
    r->Val->Integer = GetExitCodeThread((HANDLE)a[0]->Val->Pointer, (LPDWORD)a[1]->Val->Pointer) ? TRUE : FALSE;
}
static void WinInitializeCriticalSection(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    InitializeCriticalSection((LPCRITICAL_SECTION)a[0]->Val->Pointer);
}
static void WinEnterCriticalSection(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    EnterCriticalSection((LPCRITICAL_SECTION)a[0]->Val->Pointer);
}
static void WinLeaveCriticalSection(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    LeaveCriticalSection((LPCRITICAL_SECTION)a[0]->Val->Pointer);
}
static void WinDeleteCriticalSection(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    DeleteCriticalSection((LPCRITICAL_SECTION)a[0]->Val->Pointer);
}

static void WinPulseEvent(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = PulseEvent((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }

static void WinTerminateThread(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = TerminateThread((HANDLE)a[0]->Val->Pointer, (DWORD)a[1]->Val->Integer) ? TRUE : FALSE; }

static void WinSuspendThread(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)SuspendThread((HANDLE)a[0]->Val->Pointer); }

static void WinResumeThread(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)ResumeThread((HANDLE)a[0]->Val->Pointer); }

static void WinSetThreadPriority(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = SetThreadPriority((HANDLE)a[0]->Val->Pointer, (int)a[1]->Val->Integer) ? TRUE : FALSE; }

static void WinWaitForMultipleObjects(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    int gil;
    (void)p; (void)n;
    gil = BabycGuestGIL_UnlockIfHeld();
    r->Val->Integer = (long)WaitForMultipleObjects(
        (DWORD)a[0]->Val->Integer,
        (const HANDLE *)a[1]->Val->Pointer,
        (BOOL)a[2]->Val->Integer,
        (DWORD)a[3]->Val->Integer);
    BabycGuestGIL_RelockIf(gil);
}

static void WinSetFilePointerEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    LARGE_INTEGER dist;
    (void)p; (void)n;
    dist.QuadPart = a[1]->Val->LongLongInteger;
    if (a[1]->Val->LongLongInteger == 0 && a[1]->Val->Integer != 0)
        dist.QuadPart = a[1]->Val->Integer;
    r->Val->Integer = SetFilePointerEx(
        (HANDLE)a[0]->Val->Pointer,
        dist,
        (PLARGE_INTEGER)a[2]->Val->Pointer,
        (DWORD)a[3]->Val->Integer) ? TRUE : FALSE;
}

static void WinGetDriveType(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetDriveType((LPCTSTR)a[0]->Val->Pointer); }

static void WinRtlInitUnicodeString(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    (void)p; (void)n; (void)r;
    RtlInitUnicodeString((PUNICODE_STRING)a[0]->Val->Pointer,
                         (PCWSTR)a[1]->Val->Pointer);
}

static void WinCreateMutex(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = CreateMutex((LPSECURITY_ATTRIBUTES)a[0]->Val->Pointer, (BOOL)a[1]->Val->Integer, (LPCTSTR)a[2]->Val->Pointer); }
static void WinReleaseMutex(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = ReleaseMutex((HANDLE)a[0]->Val->Pointer) ? TRUE : FALSE; }
static void WinWaitForSingleObject(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    int gil;
    (void)p; (void)n;
    gil = BabycGuestGIL_UnlockIfHeld();
    r->Val->Integer = (long)WaitForSingleObject((HANDLE)a[0]->Val->Pointer, (DWORD)a[1]->Val->Integer);
    BabycGuestGIL_RelockIf(gil);
}
static void WinCreateFileMapping(struct ParseState *p, struct Value *r, struct Value **a, int n)
{
    HANDLE file = (HANDLE)a[0]->Val->Pointer;
    (void)p; (void)n;
    r->Val->Pointer = CreateFileMapping(file,
        (LPSECURITY_ATTRIBUTES)a[1]->Val->Pointer,
        (DWORD)a[2]->Val->Integer, (DWORD)a[3]->Val->Integer,
        (DWORD)a[4]->Val->Integer, (LPCTSTR)a[5]->Val->Pointer);
}
static void WinOpenFileMapping(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = OpenFileMapping((DWORD)a[0]->Val->Integer, (BOOL)a[1]->Val->Integer, (LPCTSTR)a[2]->Val->Pointer); }
static void WinMapViewOfFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Pointer = MapViewOfFile((HANDLE)a[0]->Val->Pointer, (DWORD)a[1]->Val->Integer, (DWORD)a[2]->Val->Integer, (DWORD)a[3]->Val->Integer, (SIZE_T)a[4]->Val->Integer); }
static void WinUnmapViewOfFile(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = UnmapViewOfFile((LPCVOID)a[0]->Val->Pointer) ? TRUE : FALSE; }

static void WinGetDiskFreeSpaceEx(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = GetDiskFreeSpaceEx((LPCTSTR)a[0]->Val->Pointer, (PULARGE_INTEGER)a[1]->Val->Pointer, (PULARGE_INTEGER)a[2]->Val->Pointer, (PULARGE_INTEGER)a[3]->Val->Pointer) ? TRUE : FALSE; }
static void WinGetLogicalDriveStrings(struct ParseState *p, struct Value *r, struct Value **a, int n)
{ (void)p; (void)n; r->Val->Integer = (long)GetLogicalDriveStrings((DWORD)a[0]->Val->Integer, (LPTSTR)a[1]->Val->Pointer); }

struct LibraryFunction WinFunctions[] = {
    { WinGetLastError, "DWORD GetLastError(void);" },
    { WinSetLastError, "void SetLastError(DWORD);" },
    { WinGetSystemTime, "void GetSystemTime(SYSTEMTIME *);" },
    { WinGetLocalTime, "void GetLocalTime(SYSTEMTIME *);" },
    { WinSetLocalTime, "BOOL SetLocalTime(const SYSTEMTIME *);" },
    { WinGetSystemTimeAsFileTime, "void GetSystemTimeAsFileTime(FILETIME *);" },
    { WinSystemTimeToFileTime, "BOOL SystemTimeToFileTime(const SYSTEMTIME *, FILETIME *);" },
    { WinFileTimeToSystemTime, "BOOL FileTimeToSystemTime(const FILETIME *, SYSTEMTIME *);" },
    { WinCompareFileTime, "LONG CompareFileTime(const FILETIME *, const FILETIME *);" },
    { WinFileTimeToLocalFileTime, "BOOL FileTimeToLocalFileTime(const FILETIME *, FILETIME *);" },
    { WinLocalFileTimeToFileTime, "BOOL LocalFileTimeToFileTime(const FILETIME *, FILETIME *);" },
    { WinGetSystemTimes, "BOOL GetSystemTimes(FILETIME *, FILETIME *, FILETIME *);" },
    { WinQueryPerformanceCounter, "BOOL QueryPerformanceCounter(LARGE_INTEGER *);" },
    { WinQueryPerformanceFrequency, "BOOL QueryPerformanceFrequency(LARGE_INTEGER *);" },
    { WinSleep, "void Sleep(DWORD);" },
    { WinGetTickCount, "DWORD GetTickCount(void);" },
    { WinGetCurrentProcessId, "DWORD GetCurrentProcessId(void);" },
    { WinGetCurrentThreadId, "DWORD GetCurrentThreadId(void);" },
    { WinExitProcess, "void ExitProcess(UINT);" },
    { WinCreateFile, "HANDLE CreateFile(LPCTSTR, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);" },
    { WinReadFile, "BOOL ReadFile(HANDLE, LPVOID, DWORD, LPDWORD, void *);" },
    { WinWriteFile, "BOOL WriteFile(HANDLE, LPCVOID, DWORD, LPDWORD, void *);" },
    { WinCloseHandle, "BOOL CloseHandle(HANDLE);" },
    { WinSetFilePointer, "DWORD SetFilePointer(HANDLE, LONG, PLONG, DWORD);" },
    { WinSetFilePointerEx, "BOOL SetFilePointerEx(HANDLE, LARGE_INTEGER, LARGE_INTEGER *, DWORD);" },
    { WinGetFileSize, "DWORD GetFileSize(HANDLE, LPDWORD);" },
    { WinGetFileSizeEx, "BOOL GetFileSizeEx(HANDLE, LARGE_INTEGER *);" },
    { WinGetFileTime, "BOOL GetFileTime(HANDLE, FILETIME *, FILETIME *, FILETIME *);" },
    { WinGetCurrentDirectory, "DWORD GetCurrentDirectory(DWORD, LPTSTR);" },
    { WinSetCurrentDirectory, "BOOL SetCurrentDirectory(LPCTSTR);" },
    { WinFindFirstFile, "HANDLE FindFirstFile(LPCTSTR, WIN32_FIND_DATA *);" },
    { WinFindNextFile, "BOOL FindNextFile(HANDLE, WIN32_FIND_DATA *);" },
    { WinFindClose, "BOOL FindClose(HANDLE);" },
    { WinGetEnvironmentVariable, "int GetEnvironmentVariable(char *, char *, int);" },
    { WinSetEnvironmentVariable, "int SetEnvironmentVariable(char *, char *);" },
    { WinGetTempPath, "int GetTempPath(int, char *);" },
    { WinGetModuleFileName, "int GetModuleFileName(char *, int);" },
    { WinCopyFile, "int CopyFile(char *, char *, int);" },
    { WinDeleteFile, "int DeleteFile(char *);" },
    { WinMoveFile, "int MoveFile(char *, char *);" },
    { WinCreateDirectory, "int CreateDirectory(char *);" },
    { WinRemoveDirectory, "int RemoveDirectory(char *);" },
    { WinGetComputerName, "int GetComputerName(char *, int);" },
    { WinGetUserName, "int GetUserName(char *, int);" },
    { WinBeep, "int Beep(int, int);" },
    { WinGuiUnavailable, "void MsgBox(LPCTSTR, LPCTSTR);" },
    { WinGuiUnavailable, "HWND GetForegroundWindow(void);" },
    { WinGuiGetWindowText, "void GetWindowText(HWND, LPCTSTR, int);" },
    { WinGuiUnavailable, "BOOL ShowWindow(HWND, int);" },
    { WinGuiUnavailable, "HWND FindWindow(LPCTSTR, LPCTSTR);" },
    { WinGuiUnavailable, "void ActiveWindow(HWND);" },
    { WinGuiUnavailable, "int ActiveWindowWithPattern(LPCTSTR);" },
    { WinGuiUnavailable, "void CloseWindow(HWND);" },
    { WinGuiUnavailable, "BOOL BringWindowToTop(HWND);" },
    { WinGuiUnavailable, "BOOL SetWindowPos(HWND, void *, int, int, int, int, UINT);" },
    { WinGuiUnavailable, "LRESULT SendMessage(HWND, UINT, WPARAM, LPARAM);" },
    { WinGuiUnavailable, "void MaximizeWindow(HWND);" },
    { WinGuiUnavailable, "void MinimizeWindow(HWND);" },
    { WinGuiUnavailable, "void MaximizeAllWindows(void);" },
    { WinGuiUnavailable, "void MinimizeAllWindows(void);" },
    { WinGuiUnavailable, "void SendKeyboard(char *);" },
    { WinGuiUnavailable, "BOOL SetCursorPos(int, int);" },
    { WinGuiUnavailable, "void MouseLeftClick(void);" },
    { WinGuiUnavailable, "void MouseRightClick(void);" },
    { WinGuiUnavailable, "void MouseDoubleClick(void);" },
    { WinGuiUnavailable, "int ShowCursor(BOOL);" },
    { WinGuiUnavailable, "int ShellExecute(char *, char *, char *, char *, int);" },
    { WinGuiUnavailable, "int WinExec(char *, int);" },
    { WinGuiUnavailable, "int IsWindow(HWND);" },
    { WinGuiUnavailable, "int IsWindowVisible(HWND);" },
    { WinGuiUnavailable, "int GetCursorPos(int *, int *);" },
    { WinGuiUnavailable, "int MessageBox(HWND, char *, char *, int);" },
    { WinGetStdHandle, "HANDLE GetStdHandle(int);" },
    { WinGetConsoleMode, "BOOL GetConsoleMode(HANDLE, DWORD *);" },
    { WinSetConsoleMode, "BOOL SetConsoleMode(HANDLE, DWORD);" },
    { WinFormatMessage, "DWORD FormatMessage(DWORD, LPCVOID, DWORD, DWORD, LPTSTR, DWORD, void *);" },
    { WinGlobalMemoryStatusEx, "BOOL GlobalMemoryStatusEx(MEMORYSTATUSEX *);" },
    { WinCreateEvent, "HANDLE CreateEvent(void *, BOOL, BOOL, LPCTSTR);" },
    { WinOpenEvent, "HANDLE OpenEvent(DWORD, BOOL, LPCTSTR);" },
    { WinSetEvent, "BOOL SetEvent(HANDLE);" },
    { WinResetEvent, "BOOL ResetEvent(HANDLE);" },
    { WinCreateThread, "HANDLE CreateThread(void *, unsigned long, void *, void *, DWORD, DWORD *);" },
    { WinGetExitCodeThread, "BOOL GetExitCodeThread(HANDLE, DWORD *);" },
    { WinTerminateThread, "BOOL TerminateThread(HANDLE, DWORD);" },
    { WinSuspendThread, "DWORD SuspendThread(HANDLE);" },
    { WinResumeThread, "DWORD ResumeThread(HANDLE);" },
    { WinSetThreadPriority, "BOOL SetThreadPriority(HANDLE, int);" },
    { WinPulseEvent, "BOOL PulseEvent(HANDLE);" },
    { WinWaitForMultipleObjects, "DWORD WaitForMultipleObjects(DWORD, void *, BOOL, DWORD);" },
    { WinInitializeCriticalSection, "void InitializeCriticalSection(CRITICAL_SECTION *);" },
    { WinEnterCriticalSection, "void EnterCriticalSection(CRITICAL_SECTION *);" },
    { WinLeaveCriticalSection, "void LeaveCriticalSection(CRITICAL_SECTION *);" },
    { WinDeleteCriticalSection, "void DeleteCriticalSection(CRITICAL_SECTION *);" },
    { WinCreateMutex, "HANDLE CreateMutex(void *, BOOL, LPCTSTR);" },
    { WinReleaseMutex, "BOOL ReleaseMutex(HANDLE);" },
    { WinWaitForSingleObject, "DWORD WaitForSingleObject(HANDLE, DWORD);" },
    { WinCreateFileMapping, "HANDLE CreateFileMapping(HANDLE, void *, DWORD, DWORD, DWORD, LPCTSTR);" },
    { WinOpenFileMapping, "HANDLE OpenFileMapping(DWORD, BOOL, LPCTSTR);" },
    { WinMapViewOfFile, "LPVOID MapViewOfFile(HANDLE, DWORD, DWORD, DWORD, unsigned int);" },
    { WinUnmapViewOfFile, "BOOL UnmapViewOfFile(LPCVOID);" },
    { WinGetDiskFreeSpaceEx, "BOOL GetDiskFreeSpaceEx(LPCTSTR, PULARGE_INTEGER, PULARGE_INTEGER, PULARGE_INTEGER);" },
    { WinGetLogicalDriveStrings, "DWORD GetLogicalDriveStrings(DWORD, LPTSTR);" },
    { WinGetDriveType, "UINT GetDriveType(LPCTSTR);" },
    { WinRtlInitUnicodeString, "void RtlInitUnicodeString(void *, void *);" },
    { NULL, NULL }
};

void WinSetupFunc(Picoc *pc)
{
    char *guard = TableStrRegister(pc, "__win_api_loaded");
    if (VariableDefined(pc, guard)) return;
    VariableDefine(pc, NULL, guard, NULL, &pc->VoidType, FALSE);
    BabycGuestSetPC(pc);
    PicocParse(pc, "windows.h", WinDefs, strlen(WinDefs), TRUE, TRUE, FALSE, FALSE);
    PicocParse(pc, "winerror.h", WinErrorDefs, strlen(WinErrorDefs), TRUE, TRUE, FALSE, FALSE);
    LibraryAdd(pc, &pc->GlobalTable, "windows.h", WinFunctions);
    VariableDefinePlatformVar(pc, NULL, "STD_INPUT_HANDLE", &pc->IntType,
        (union AnyValue *)&S_STD_INPUT_HANDLE, FALSE);
    VariableDefinePlatformVar(pc, NULL, "STD_OUTPUT_HANDLE", &pc->IntType,
        (union AnyValue *)&S_STD_OUTPUT_HANDLE, FALSE);
    VariableDefinePlatformVar(pc, NULL, "STD_ERROR_HANDLE", &pc->IntType,
        (union AnyValue *)&S_STD_ERROR_HANDLE, FALSE);
}
#endif
