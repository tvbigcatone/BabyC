#include "windows.h"
#include "wprivate.h"

#if(ENABLE_WPROCESS == 1)

VOID WINAPI ExitProcess(
  _In_ UINT uExitCode
)
{
   exit(uExitCode);
}

#endif