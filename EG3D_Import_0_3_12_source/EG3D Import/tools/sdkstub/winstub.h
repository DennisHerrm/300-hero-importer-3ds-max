// ============================================================
//  winstub.h - Windows-Grundtypen fuer die Stub-Uebersetzung
//
//  ATTRAPPE. Siehe tools/sdkstub/README.md.
//
//  Unter Windows tut dieser Header nichts: dort gibt es das
//  echte <windows.h>, und der Stub soll dieselben Typen sehen
//  wie der echte Build. Nur unter Linux werden sie hier
//  nachgebildet, damit die Pruefung auch dort laeuft.
// ============================================================
#ifndef EG3D_WINSTUB_H
#define EG3D_WINSTUB_H

#ifdef _WIN32
#include <windows.h>
#else

#include <cstddef>
#include <cstdint>
#include <cwchar>

using BOOL = int;
using DWORD = unsigned long;
using ULONG = unsigned long;
using UINT = unsigned int;
using WORD = unsigned short;
using BYTE = unsigned char;
using LPVOID = void*;
using LPCVOID = const void*;

struct HINSTANCE__ { int unused; };
using HINSTANCE = HINSTANCE__*;
using HMODULE = HINSTANCE;
struct HWND__ { int unused; };
using HWND = HWND__*;
struct HANDLE__ { int unused; };
using HANDLE = HANDLE__*;
struct HRSRC__ { int unused; };
using HRSRC = HRSRC__*;
struct HGLOBAL__ { int unused; };
using HGLOBAL = HGLOBAL__*;

using TCHAR = wchar_t;
using LPCTSTR = const wchar_t*;
using LPWSTR = wchar_t*;
using LPCWSTR = const wchar_t*;

#define TRUE 1
#define FALSE 0
#define MAX_PATH 260
#define WINAPI
#define __declspec(x)
#define DLL_PROCESS_ATTACH 1
#define INVALID_HANDLE_VALUE (reinterpret_cast<HANDLE>(-1))
#define INVALID_FILE_ATTRIBUTES (static_cast<DWORD>(-1))
#define GENERIC_WRITE 0x40000000
#define CREATE_ALWAYS 2
#define FILE_ATTRIBUTE_NORMAL 0x80
#define RT_RCDATA reinterpret_cast<const wchar_t*>(10)
#define MAKEINTRESOURCE(i) reinterpret_cast<const wchar_t*>(static_cast<size_t>(i))
#define CP_ACP 0
#define CP_UTF8 65001
#define MB_ICONERROR 0x10
#define MB_OK 0

inline void DisableThreadLibraryCalls(HINSTANCE) {}
inline HRSRC FindResource(HINSTANCE, const wchar_t*, const wchar_t*) { return nullptr; }
inline HGLOBAL LoadResource(HINSTANCE, HRSRC) { return nullptr; }
inline const void* LockResource(HGLOBAL) { return nullptr; }
inline DWORD SizeofResource(HINSTANCE, HRSRC) { return 0; }
inline DWORD GetTempPathW(DWORD, wchar_t*) { return 0; }
inline HANDLE CreateFileW(const wchar_t*, DWORD, DWORD, void*, DWORD, DWORD, HANDLE) {
    return INVALID_HANDLE_VALUE;
}
inline BOOL WriteFile(HANDLE, const void*, DWORD, DWORD*, void*) { return FALSE; }
inline BOOL CloseHandle(HANDLE) { return TRUE; }
inline DWORD GetEnvironmentVariableW(const wchar_t*, wchar_t*, DWORD) { return 0; }
inline DWORD GetFileAttributesW(const wchar_t*) { return INVALID_FILE_ATTRIBUTES; }
inline int MessageBoxW(HWND, const wchar_t*, const wchar_t*, UINT) { return 0; }
inline int MultiByteToWideChar(UINT, DWORD, const char*, int, wchar_t*, int) { return 0; }
inline int WideCharToMultiByte(UINT, DWORD, const wchar_t*, int, char*, int,
                               const char*, BOOL*) { return 0; }

#endif  // _WIN32
#endif  // EG3D_WINSTUB_H
