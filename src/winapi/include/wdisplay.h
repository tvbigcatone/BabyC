
/***********************************************************************************************/
/*                                             DISPLAY                                         */
/***********************************************************************************************/

#define DISPLAY_DEVICE_ATTACHED_TO_DESKTOP   0x00000001
#define DISPLAY_DEVICE_MULTI_DRIVER          0x00000002
#define DISPLAY_DEVICE_PRIMARY_DEVICE        0x00000004
#define DISPLAY_DEVICE_MIRRORING_DRIVER      0x00000008
#define DISPLAY_DEVICE_VGA_COMPATIBLE        0x00000010

#define DM_BITSPERPEL	                     0x40000
#define DM_PELSWIDTH	                        0x80000
#define DM_PELSHEIGHT	                     0x100000
#define DM_DISPLAYFLAGS                      0x200000
#define DM_DISPLAYFREQUENCY                  0x400000

#define CDS_UPDATEREGISTRY	                  1
#define CDS_TEST	                           2
#define CDS_FULLSCREEN	                     4   
#define CDS_GLOBAL	                        8 
#define CDS_SET_PRIMARY	                     16
#define CDS_RESET	                           0x40000000 
#define CDS_SETRECT	                        0x20000000 
#define CDS_NORESET	                        0x10000000 

#define DISP_CHANGE_SUCCESSFUL	            0
#define DISP_CHANGE_RESTART	               1
#define DISP_CHANGE_BADFLAGS	               (-4)
#define DISP_CHANGE_BADPARAM	               (-5)
#define DISP_CHANGE_FAILED	                  (-1)
#define DISP_CHANGE_BADMODE	               (-2)
#define DISP_CHANGE_NOTUPDATED	            (-3)

#define BST_CHECKED	                        1
#define BST_INDETERMINATE	                  2
#define BST_UNCHECKED	                     0
#define BST_FOCUS	                           8
#define BST_PUSHED	                        4

#define CCHDEVICENAME                        32
#define CCHFORMNAME                          32

#define ENUM_CURRENT_SETTINGS                -1
#define ENUM_REGISTRY_SETTINGS               -2

typedef struct _DISPLAY_DEVICE {
  DWORD cb;
  TCHAR DeviceName[32];
  TCHAR DeviceString[128];
  DWORD StateFlags;
  TCHAR DeviceID[128];
  TCHAR DeviceKey[128];
} DISPLAY_DEVICE, *PDISPLAY_DEVICE, *LPDISPLAY_DEVICE;

typedef struct _POINTL {
  LONG x;
  LONG y;
} POINTL, *PPOINTL, *LPPOINTL;

typedef struct _devicemode {
  TCHAR dmDeviceName[CCHDEVICENAME];
  WORD  dmSpecVersion;
  WORD  dmDriverVersion;
  WORD  dmSize;
  WORD  dmDriverExtra;
  DWORD dmFields;
  union {
    struct {
      short dmOrientation;
      short dmPaperSize;
      short dmPaperLength;
      short dmPaperWidth;
      short dmScale;
      short dmCopies;
      short dmDefaultSource;
      short dmPrintQuality;
    };
    struct {
      POINTL dmPosition;
      DWORD  dmDisplayOrientation;
      DWORD  dmDisplayFixedOutput;
    };
  };
  short dmColor;
  short dmDuplex;
  short dmYResolution;
  short dmTTOption;
  short dmCollate;
  TCHAR dmFormName[CCHFORMNAME];
  WORD  dmLogPixels;
  DWORD dmBitsPerPel;
  DWORD dmPelsWidth;
  DWORD dmPelsHeight;
  union {
    DWORD dmDisplayFlags;
    DWORD dmNup;
  };
  DWORD dmDisplayFrequency;
#if (WINVER >= 0x0400)
  DWORD dmICMMethod;
  DWORD dmICMIntent;
  DWORD dmMediaType;
  DWORD dmDitherType;
  DWORD dmReserved1;
  DWORD dmReserved2;
#if (WINVER >= 0x0500) || (_WIN32_WINNT >= 0x0400)
  DWORD dmPanningWidth;
  DWORD dmPanningHeight;
#endif 
#endif 
} DEVMODE, *PDEVMODE, *LPDEVMODE;

BOOL EnumDisplayDevices(
  _In_  LPCTSTR         lpDevice,
  _In_  DWORD           iDevNum,
  _Out_ PDISPLAY_DEVICE lpDisplayDevice,
  _In_  DWORD           dwFlags
);

BOOL EnumDisplaySettings(
  _In_  LPCTSTR lpszDeviceName,
  _In_  DWORD   iModeNum,
  _Out_ DEVMODE *lpDevMode
);

LONG ChangeDisplaySettings(
  _In_ DEVMODE *lpDevMode,
  _In_ DWORD   dwflags
);