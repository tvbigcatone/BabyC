#ifndef __WCONFIG_H__
#define __WCONFIG_H__

/* Moduli opzionali: ridefinibili da riga di comando (-DENABLE_WTHREADS=1 ...). */
#ifndef ENABLE_WTHREADS
#define ENABLE_WTHREADS (0)
#endif
#ifndef ENABLE_WSHMEM
#define ENABLE_WSHMEM   (0)
#endif
#ifndef ENABLE_WERROR
#define ENABLE_WERROR   (1)
#endif
#ifndef ENABLE_WSTATS
#define ENABLE_WSTATS   (1)
#endif
#ifndef ENABLE_WPERF
#define ENABLE_WPERF    (1)
#endif
#ifndef ENABLE_DISPLAY
#define ENABLE_DISPLAY  (0)
#endif
#ifndef ENABLE_WPROCESS
#define ENABLE_WPROCESS (1)
#endif

/* Dipendenze esterne opzionali. */
#ifndef ENABLE_UDEV   /* libudev: rilevamento dischi USB in GetDriveType */
#define ENABLE_UDEV     (0)
#endif
#ifndef ENABLE_X11    /* X11: MessageBox grafica */
#define ENABLE_X11      (0)
#endif

#endif
