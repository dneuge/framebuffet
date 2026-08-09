#ifndef FBSOURCE_CAPTURE_WINRT_DXGI_FOREIGN_API_H
#define FBSOURCE_CAPTURE_WINRT_DXGI_FOREIGN_API_H

#ifdef TARGET_WINDOWS
#error foreign API signatures must not be included when compiling for actual target platform
#else
#include "stdlib.h"

#define BOOL int
#define LPARAM long long int
#define LPTSTR char*
#define HWND void*
#define HRESULT int
#define DWORD int32_t
#define UINT uint16_t

typedef struct {
    // NOTE: actual types are undocumented
    int top;
    int bottom;
    int left;
    int right;
} RECT;

#define LPRECT RECT*

typedef BOOL (*WNDENUMPROC)(HWND hwnd, LPARAM lParam);

static BOOL EnumWindows(WNDENUMPROC lpEnumFunc, LPARAM lParam);
static int GetWindowTextLengthA(HWND hWnd);
static int GetWindowTextA(HWND hWnd, LPTSTR lpString, int nMaxCount);
static int GetClassNameA(HWND hWnd, LPTSTR lpClassName, int nMaxCount);
static HWND GetShellWindow();
static BOOL IsWindowVisible(HWND hWnd);
static DWORD GetWindowLongW(HWND hWnd, int nIndex);
static HWND GetAncestor(HWND hwnd, UINT gaFlags);
static HRESULT DwmGetWindowAttribute(HWND hwnd, DWORD dwAttribute, void *pvAttribute, size_t cbAttribute);
static BOOL GetClientRect(HWND hWnd, LPRECT lpRect);

// NOTE: these constants are entirely made up
#define GA_ROOT 42
#define GWL_EXSTYLE 123
#define GWL_STYLE 456
#define WS_VISIBLE 1<<20
#define WS_DISABLED 1<<18
#define WS_EX_TOOLWINDOW 1<<12
#define DWMWA_CLOAKED 123123
#define DWM_CLOAKED_SHELL 98765
#define DWMWA_EXTENDED_FRAME_BOUNDS 3333
#define S_OK 111

#define FALSE 0
#define TRUE 1
#endif

#endif //FBSOURCE_CAPTURE_WINRT_DXGI_FOREIGN_API_H
