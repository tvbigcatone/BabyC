/* all platform-specific includes and defines go in this file */
#ifndef PLATFORM_H
#define PLATFORM_H

/* configurable options */
/* select your host type (or do it in the Makefile):
 * #define  UNIX_HOST
 * #define  FLYINGFOX_HOST
 * #define  SURVEYOR_HOST
 * #define  SRV1_UNIX_HOST
 * #define  UMON_HOST
 * #define  WIN32  (predefined on MSVC)
 */

#define LARGE_INT_POWER_OF_TEN 1000000000   /* the largest power of ten which fits in an int on this architecture */
#if defined(__hppa__) || defined(__sparc__)
#define ALIGN_TYPE double                   /* the default data type to use for alignment */
#else
#define ALIGN_TYPE void *                   /* the default data type to use for alignment */
#endif

#define GLOBAL_TABLE_SIZE 97                /* global variable table */
#define STRING_TABLE_SIZE 97                /* shared string table size */
#define STRING_LITERAL_TABLE_SIZE 97        /* string literal table size */
#define RESERVED_WORD_TABLE_SIZE 97         /* reserved word table size */
#define PARAMETER_MAX 16                    /* maximum number of parameters to a function */
#define LINEBUFFER_MAX 256                  /* maximum number of characters on a line */
#define LOCAL_TABLE_SIZE 11                 /* size of local variable table (can expand) */
#define STRUCT_TABLE_SIZE 11                /* size of struct/union member table (can expand) */

#define INTERACTIVE_PROMPT_START ""  /* full banner via PicocPrintBanner() */
#ifndef PERSOPORT
#define INTERACTIVE_PROMPT_STATEMENT "picoc> "
#define INTERACTIVE_PROMPT_LINE "     > "
#else
extern char INTERACTIVE_PROMPT_STATEMENT[32] ;
extern char INTERACTIVE_PROMPT_LINE[32] ;
#endif

/* host platform includes */
#ifdef UNIX_HOST
# define USE_MALLOC_STACK                   /* stack is allocated using malloc() */
# define USE_MALLOC_HEAP                    /* heap is allocated using malloc() */
# include <stdio.h>
# include <stdlib.h>
# include <ctype.h>
# include <string.h>
# include <assert.h>
# include <sys/types.h>
# include <sys/stat.h>
# include <unistd.h>
# include <stdarg.h>
# include <setjmp.h>
# ifndef NO_FP
#  include <math.h>
#  define PICOC_MATH_LIBRARY
/* #  define USE_READLINE */
#  undef BIG_ENDIAN
#  if defined(__powerpc__) || defined(__hppa__) || defined(__sparc__)
#   define BIG_ENDIAN
#  endif
# endif

#ifdef PLATFORM_32BIT
typedef unsigned long       PTR_TYPE_INT;
typedef long                PRT_TYPE_INT_SIGNED;
#else
typedef unsigned long long	PTR_TYPE_INT;
typedef long long           PTR_TYPE_INT_SIGNED;
#endif

/* LP64: long is 64-bit — no value-changing narrow from PTR_TYPE */
# define BABYC_NARROW_LONG(x)    (x)
# define BABYC_NARROW_ULONG(x)   ((unsigned long)(x))
# define BABYC_LL_TRUE(x)        ((x) != 0)
# define BABYC_LL_FALSE(x)       ((x) == 0)

extern jmp_buf ExitBuf;

#else
# ifdef WIN32
#  define USE_MALLOC_STACK                   /* stack is allocated using malloc() */
#  define USE_MALLOC_HEAP                    /* heap is allocated using malloc() */
#  include <stdint.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <ctype.h>
#  include <string.h>
#  include <assert.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <stdarg.h>
#  include <setjmp.h>
#  include <math.h>
#  define PICOC_MATH_LIBRARY
#  undef BIG_ENDIAN
   /* MSVC / Windows CRT: POSIX names map to ISO/MS conformant names */
#  ifndef strcasecmp
#   define strcasecmp   _stricmp
#  endif
#  ifndef strncasecmp
#   define strncasecmp  _strnicmp
#  endif
#  ifndef strdup
#   define strdup       _strdup
#  endif
#  ifndef isatty
#   include <io.h>
#   define isatty       _isatty
#  endif

typedef unsigned long long	PTR_TYPE_INT;
typedef long long           PTR_TYPE_INT_SIGNED;

   /* LLP64: long is 32-bit, PTR_TYPE is 64-bit */
#  define BABYC_NARROW_LONG(x)    ((long)(x))
#  define BABYC_NARROW_ULONG(x)   ((unsigned long)(x))
#  define BABYC_LL_TRUE(x)        ((x) != 0)
#  define BABYC_LL_FALSE(x)       ((x) == 0)

extern jmp_buf ExitBuf;

# else
#  ifdef FLYINGFOX_HOST
#   define HEAP_SIZE (16*1024)               /* space for the heap and the stack */
#   define NO_HASH_INCLUDE
#   include <stdlib.h>
#   include <ctype.h>
#   include <string.h>
#   include <sys/types.h>
#   include <stdarg.h>
#   include <setjmp.h>
#   include <math.h>
#   define assert(x)
#   define BUILTIN_MINI_STDLIB
#   undef BIG_ENDIAN

#  else
#   ifdef SURVEYOR_HOST
#    define HEAP_SIZE C_HEAPSIZE
#    define NO_FP
#    define NO_CTYPE
#    define NO_HASH_INCLUDE
#    define NO_MODULUS
#    include <cdefBF537.h>
#    include "../string.h"
#    include "../print.h"
#    include "../srv.h"
#    include "../setjmp.h"
#    include "../stdarg.h"
#    include "../colors.h"
#    include "../neural.h"
#    include "../gps.h"
#    include "../i2c.h"
#    include "../jpeg.h"
#    include "../malloc.h"
#    include "../xmodem.h"
#    define assert(x)
#    undef BIG_ENDIAN
#    define NO_CALLOC
#    define NO_REALLOC
#    define BROKEN_FLOAT_CASTS
#    define BUILTIN_MINI_STDLIB
#   else
#    ifdef UMON_HOST
#     define HEAP_SIZE (128*1024)               /* space for the heap and the stack */
#     define NO_FP
#     define BUILTIN_MINI_STDLIB
#     include <stdlib.h>
#     include <string.h>
#     include <ctype.h>
#     include <sys/types.h>
#     include <stdarg.h>
#     include <math.h>
#     include "monlib.h"
#     define assert(x)
#     define malloc mon_malloc
#     define calloc(a,b) mon_malloc(a*b)
#     define realloc mon_realloc
#     define free mon_free
#    endif
#   endif
#  endif

extern int ExitBuf[];

# endif
#endif

/* Central target integer model. These expressions intentionally use the
 * compiler's target types: LP64 yields 64-bit long; MSVC LLP64 yields
 * 32-bit long while pointers remain 64-bit. CHAR_MIN documents the target's
 * plain-char signedness instead of assuming signed char in the evaluator. */
#include <limits.h>
#define BABYC_VALUE_BITS_CHAR       ((int)(sizeof(char) * CHAR_BIT))
#define BABYC_VALUE_BITS_SHORT      ((int)(sizeof(short) * CHAR_BIT))
#define BABYC_VALUE_BITS_INT        ((int)(sizeof(int) * CHAR_BIT))
#define BABYC_VALUE_BITS_LONG       ((int)(sizeof(long) * CHAR_BIT))
#define BABYC_VALUE_BITS_LLONG      ((int)(sizeof(long long) * CHAR_BIT))
#define BABYC_VALUE_BITS_POINTER    ((int)(sizeof(void *) * CHAR_BIT))
#define BABYC_PLAIN_CHAR_IS_SIGNED  (CHAR_MIN < 0)

#endif /* PLATFORM_H */

/* Fallback if a host branch forgot the helpers */
#ifndef BABYC_NARROW_LONG
# define BABYC_NARROW_LONG(x)    ((long)(x))
# define BABYC_NARROW_ULONG(x)   ((unsigned long)(x))
#endif
#ifndef BABYC_LL_TRUE
# define BABYC_LL_TRUE(x)        ((x) != 0)
# define BABYC_LL_FALSE(x)       ((x) == 0)
#endif
