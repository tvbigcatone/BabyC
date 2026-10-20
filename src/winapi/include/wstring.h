#ifdef _UNICODE
#define L(str) L##str
#define TEXT(str) L##str
#else
#define L(str) str
#define TEXT(str) str
#endif

#define _snprintf  snprintf 
#define lstrlen      wcslen

// #define lstrlen                       strlen
#define lstrcpy                          strcpy
#define lstrcmpi                         strcasecmp
#define _stricmp                         strcasecmp
#define InterlockedIncrement(x)          __sync_fetch_and_add((x), 1)
#define InterlockedDecrement(x)          __sync_fetch_and_sub((x), 1)

#define stricmp(s1, s2) strcasecmp(s1, s2)
#define strnicmp(s1, s2, n) strncasecmp(s1, s2, n)

char *strupr(char *str);
char *strlwr(char *str);

VOID WINAPI RtlInitUnicodeString(
  _Inout_  PUNICODE_STRING DestinationString,
  _In_opt_ PCWSTR          SourceString
);

char *str_replace(const char* string, const char* substr, const char* replacement);
void chr_replace (char *s, char c, char r) ;