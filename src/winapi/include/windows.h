#ifndef __WINAPI__
#define __WINAPI__

#include "wconfig.h"

#include <stdlib.h> // free
#include <stdio.h>
#include <mntent.h>
#include <unistd.h>
#include <limits.h>
#include <string.h> // strcpy
#include <sys/resource.h> // rtlimits
#include <libgen.h>
#include <inttypes.h>
#include <wchar.h> // wcslen
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/queue.h>
# ifndef _GNU_SOURCE
   #define _GNU_SOURCE             /* See feature_test_macros(7) */
#endif
#include <pthread.h>
// #define _POSIX_SOURCE
#include <signal.h>
#include <semaphore.h>
#include <glob.h>
#include <errno.h> // EBUSY ecc.

#include "windef.h"
#include "werror.h"
#include "wthreads.h"
#include "wprocess.h"
#include "wshmem.h"
#include "wperf.h"
#include "wstats.h"
#include "wstring.h"
#include "wtime.h"
#include "wfiles.h"
#include "wdisplay.h"
#include "wmsgbox.h"

#endif