
#ifndef __MINI_PRINTF_H__
#define __MINI_PRINTF_H__

/*-----------------------------------------------+-----------------------------------------------------------*
 * 											INCLUDE FILES
 *-----------------------------------------------------------------------------------------------------------*/
#include <stdarg.h>

/*-----------------------------------------------+-----------------------------------------------------------*
 * 											EXPORTED FUNCTIONS
 *-----------------------------------------------------------------------------------------------------------*/

#if 1
int          mini_snprintf(char* buffer, unsigned int buffer_len, const char *fmt, ...);
int          mini_vsnprintf(char *buffer, unsigned int buffer_len, const char *fmt, va_list va);
unsigned int mini_strlen(const char *s);
/* no macro hijack */
#endif

#endif // __MINI_PRINTF_H__

