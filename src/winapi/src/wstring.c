
#include "windows.h"

#include <ctype.h> // toupper, tolower

/***********************************************************************************************/
/*                                          HELPERS                                            */
/***********************************************************************************************/

// fork from https://github.com/irl/la-cucina/blob/master/str_replace.c
char* str_replace(const char* str, const char* substr, const char* replacement)
{
    const char *scan;
    const char *match;
    char *result;
    char *out;
    size_t source_len;
    size_t substr_len;
    size_t replacement_len;
    size_t occurrences = 0;
    size_t result_len;

    if (str == NULL)
        return NULL;
    if (substr == NULL || replacement == NULL)
        return strdup(str);

    source_len = strlen(str);
    substr_len = strlen(substr);
    replacement_len = strlen(replacement);
    /* An empty search string has no useful finite replacement semantics here. */
    if (substr_len == 0)
        return strdup(str);

    for (scan = str; (match = strstr(scan, substr)) != NULL;
         scan = match + substr_len) {
        ++occurrences;
    }

    if (replacement_len >= substr_len) {
        size_t growth = replacement_len - substr_len;
        if (growth != 0 && occurrences > (SIZE_MAX - source_len - 1) / growth) {
            errno = ENOMEM;
            return NULL;
        }
        result_len = source_len + occurrences * growth;
    } else {
        result_len = source_len - occurrences * (substr_len - replacement_len);
    }

    result = (char *)malloc(result_len + 1);
    if (result == NULL)
        return NULL;

    scan = str;
    out = result;
    while ((match = strstr(scan, substr)) != NULL) {
        size_t prefix_len = (size_t)(match - scan);
        memcpy(out, scan, prefix_len);
        out += prefix_len;
        memcpy(out, replacement, replacement_len);
        out += replacement_len;
        scan = match + substr_len;
    }
    memcpy(out, scan, strlen(scan) + 1);
    return result;
}

void chr_replace (char *s, char c, char r)  
{ 
   char *p1;
 
   for( p1 = s; *p1; p1++ )
   {
      if (*p1 == c) *p1 = r;
   }
}

char *strupr(char *str)
{
   unsigned char *p = (unsigned char *)str;
   while (*p) { if( isalpha(*p) && !isupper(*p)) *p = toupper(*p); p++; } 
   return str;
}

char *strlwr(char *str)
{
   unsigned char *p = (unsigned char *)str;
   while (*p) { if (isalpha(*p) && !islower(*p)) *p = tolower(*p); p++; } 
   return str;
}

VOID WINAPI RtlInitUnicodeString(
  _Inout_  PUNICODE_STRING DestinationString,
  _In_opt_ PCWSTR          SourceString
)
{
    size_t chars, max_chars;
    USHORT length;
    if (DestinationString == NULL)
        return;

    if (SourceString == NULL) {
        DestinationString->Buffer = NULL;
        DestinationString->Length = 0;
        DestinationString->MaximumLength = 0;
        return;
    }

    /* UNICODE_STRING lengths are byte counts, exclude the terminator from
     * Length, include room for it in MaximumLength, and must not wrap USHORT. */
    max_chars = ((size_t)USHRT_MAX - sizeof(WCHAR)) / sizeof(WCHAR);
    chars = wcslen(SourceString);
    if (chars > max_chars) chars = max_chars;
    length = (USHORT)(chars * sizeof(WCHAR));
    DestinationString->Buffer = (PWSTR)SourceString;
    DestinationString->Length = length;
    DestinationString->MaximumLength = (USHORT)(length + sizeof(WCHAR));
}
