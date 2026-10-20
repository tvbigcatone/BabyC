
#include "windows.h"
#include "wprivate.h"

#if(ENABLE_DISPLAY == 1)

#include <stdio.h>
#include <dirent.h>
#include <string.h>
#include <X11/Xlib.h>


static int display_endpoint_equal(const char *candidate, const char *configured)
{
   char a[64], b[64];
   size_t alen, blen;
   if (candidate == NULL || configured == NULL) return 0;
   /* Xlib accepts both :N and unix:N forms. Ignore the optional screen suffix
    * because the socket enumeration identifies the display, not its screen. */
   if (strncmp(candidate, "unix:", 5) == 0) candidate += 5;
   if (strncmp(configured, "unix:", 5) == 0) configured += 5;
   alen = strcspn(candidate, ".");
   blen = strcspn(configured, ".");
   if (alen == 0 || blen == 0 || alen >= sizeof(a) || blen >= sizeof(b)) return 0;
   memcpy(a, candidate, alen); a[alen] = '\0';
   memcpy(b, configured, blen); b[blen] = '\0';
   return strcmp(a, b) == 0;
}

BOOL EnumDisplayDevices(
  _In_  LPCTSTR         lpDevice,
  _In_  DWORD           iDevNum,
  _Out_ PDISPLAY_DEVICE lpDisplayDevice,
  _In_  DWORD           dwFlags
)
{
   DWORD currDevNum = 0;
   DIR* d;
   Display *default_display;
   BOOL found = FALSE;

   if (lpDisplayDevice == NULL || lpDisplayDevice->cb < sizeof(*lpDisplayDevice) ||
       dwFlags != 0) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }

   d = opendir("/tmp/.X11-unix");
   default_display = XOpenDisplay(NULL);
   if (d != NULL) {
      struct dirent *dr;
      while ((dr = readdir(d)) != NULL) {
         char display_name[64];
         int written;
         Display *display;
         if (dr->d_name[0] != 'X') continue;
         written = snprintf(display_name, sizeof(display_name), ":%s", dr->d_name + 1);
         if (written < 0 || (size_t)written >= sizeof(display_name) ||
             (size_t)written >= sizeof(lpDisplayDevice->DeviceName))
            continue;

         display = XOpenDisplay(display_name);
         if (display == NULL) continue;
         if ((lpDevice != NULL && display_endpoint_equal(lpDevice, display_name)) ||
             (lpDevice == NULL && iDevNum == currDevNum)) {
            int is_default = default_display != NULL &&
               display_endpoint_equal(display_name, DisplayString(default_display));
            memset(lpDisplayDevice->DeviceName, 0, sizeof(lpDisplayDevice->DeviceName));
            memcpy(lpDisplayDevice->DeviceName, display_name, (size_t)written + 1);
            snprintf(lpDisplayDevice->DeviceString, sizeof(lpDisplayDevice->DeviceString),
                     "%s", "X 11 Windowing System");
            lpDisplayDevice->StateFlags = DISPLAY_DEVICE_VGA_COMPATIBLE;
            if (is_default)
               lpDisplayDevice->StateFlags |= DISPLAY_DEVICE_ATTACHED_TO_DESKTOP |
                                              DISPLAY_DEVICE_PRIMARY_DEVICE;
            memset(lpDisplayDevice->DeviceID, 0, sizeof(lpDisplayDevice->DeviceID));
            memset(lpDisplayDevice->DeviceKey, 0, sizeof(lpDisplayDevice->DeviceKey));
            found = TRUE;
            XCloseDisplay(display);
            break;
         }
         XCloseDisplay(display);
         ++currDevNum;
      }
      closedir(d);
   }

   /* If the default display is available but socket enumeration did not find
    * it, expose the single default device. Avoid returning success from the old
    * no-op display-settings stubs below: they do not actually change modes. */
   if (!found && lpDevice == NULL && iDevNum == 0 && default_display != NULL) {
      int written = snprintf(lpDisplayDevice->DeviceName,
                            sizeof(lpDisplayDevice->DeviceName), "%s", "X11");
      if (written >= 0 && (size_t)written < sizeof(lpDisplayDevice->DeviceName)) {
         snprintf(lpDisplayDevice->DeviceString, sizeof(lpDisplayDevice->DeviceString),
                  "%s", "X 11 Windowing System");
         lpDisplayDevice->StateFlags = DISPLAY_DEVICE_ATTACHED_TO_DESKTOP |
                                       DISPLAY_DEVICE_PRIMARY_DEVICE |
                                       DISPLAY_DEVICE_VGA_COMPATIBLE;
         memset(lpDisplayDevice->DeviceID, 0, sizeof(lpDisplayDevice->DeviceID));
         memset(lpDisplayDevice->DeviceKey, 0, sizeof(lpDisplayDevice->DeviceKey));
         found = TRUE;
      }
   }
   if (default_display != NULL) XCloseDisplay(default_display);
   SetLastError(found ? ERROR_SUCCESS : ERROR_NO_MORE_FILES);
   return found;
}

BOOL EnumDisplaySettings(
  _In_  LPCTSTR lpszDeviceName,
  _In_  DWORD   iModeNum,
  _Out_ DEVMODE *lpDevMode
)
{
   (void)lpszDeviceName;
   (void)iModeNum;
   if (lpDevMode == NULL) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
   }
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return FALSE;
}

LONG ChangeDisplaySettings(
  _In_ DEVMODE *lpDevMode,
  _In_ DWORD   dwflags
)
{
   (void)lpDevMode;
   (void)dwflags;
   SetLastError(ERROR_CALL_NOT_IMPLEMENTED);
   return DISP_CHANGE_FAILED;
}

#endif