// define this macro for activating debugging version
//#define GETHOSTNAME_LOCAL_DEBUG     1

#include <windows.h>
#include <tchar.h>
#include <stdarg.h>

#ifndef GETHOSTNAME_LOCAL_DEBUG
#include "pm.h"
#endif
#include "gethostname.h"

#define BUFSIZE 80

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);

typedef struct {
    char  str[256];
    int   level;
} push_string_t;

static void
pushString(push_string_t *p, char *fmt, ...) a{
    va_list args;

    va_start(args, fmt);
    p->level += _vsnprintf(p->str + p->level, sizeof(p->str)-p->level,
                           fmt, args);
    va_end(args);
}



static void
getVersion(OSVERSIONINFOEX * osviP,
           SYSTEM_INFO *     siP,
           BOOL *            succeededP) {

    PGNSI pGNSI;

    ZeroMemory(siP, sizeof(SYSTEM_INFO));
    ZeroMemory(osviP, sizeof(OSVERSIONINFOEX));

    // Try calling GetVersionEx using the OSVERSIONINFOEX structure.
    // If that fails, try using the OSVERSIONINFO structure.
    *succeededP = TRUE;  // initial value
    osviP->dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if (!(bOsVersionInfoEx = GetVersionEx ((OSVERSIONINFO *) osviP))) {
        osvi.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
        if (!GetVersionEx((OSVERSIONINFO *) osviP)) 
            *succededP = FALSE;
    }

    if (*succeededP) {
        // Call GetNativeSystemInfo if available; GetSystemInfo otherwise.
        pGNSI = (PGNSI)
            GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), 
                           "GetNativeSystemInfo");
        if (NULL != pGNSI)
            pGNSI(siP);
        else
            GetSystemInfo(siP);

        *succeededP = TRUE;
    }
}



static void
getStringVersion2(OSVERSIONINFOEX osvi,
                  SYSTEM_INFO     si,
                  push_string_t * str) {

    if (si.wProcessorArchitecture ==
        PROCESSOR_ARCHITECTURE_IA64) {
        if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
            pushString(str, "Datacenter Edition for Itanium-based Systems");
        else if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
            pushString(str, "Enterprise Edition for Itanium-based Systems");
    } else if (si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64) {
        if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
            pushString(str, "Datacenter x64 Edition ");
        else if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
            pushString(str, "Enterprise x64 Edition ");
        else
            pushString(str, "Standard x64 Edition ");
    } else {
        if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
            pushString(str, "Datacenter Edition ");
        else if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
            pushString(str, "Enterprise Edition ");
        else if (osvi.wSuiteMask & VER_SUITE_BLADE)
            pushString(str, "Web Edition ");
        else
            pushString(str, "Standard Edition ");
    }
}



static void
getStringVersion3(OSVERSIONINFOEX osvi,
                  SYSTEM_INFO     si,
                  push_string_t * str) {

    // Test for specific product on Windows NT 4.0 SP6 and later.
    // Test for the workstation type.

    if (osvi.wProductType == VER_NT_WORKSTATION &&
        si.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64) {
        if (osvi.dwMajorVersion == 4)
            pushString(str, "Workstation 4.0 ");
        else if (osvi.wSuiteMask & VER_SUITE_PERSONAL)
            pushString(str, "Home Edition ");
        else
            pushString(str, "Professional ");
    } else if (osvi.wProductType == VER_NT_SERVER || 
               osvi.wProductType == VER_NT_DOMAIN_CONTROLLER) {
        if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2)
            getStringVersion2(osvi, si, str);
        else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0) {
            if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
                pushString(str, "Datacenter Server ");
            else if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                pushString(str, "Advanced Server ");
            else
                pushString(str, "Server " );
        } else {
            // Windows NT 4.0 
            if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                pushString(str, "Server 4.0, Enterprise Edition ");
            else
                pushString(str, "Server 4.0 ");
        }
    }
}



static void
getStringVersion4(OSVERSIONINFOEX osvi,
                  push_string_t * str,
                  BOOL *          succeededP) {

    // Test for specific product on Windows NT 4.0 SP5 and earlier

    HKEY hKey;
    TCHAR szProductType[BUFSIZE];
    DWORD dwBufLen = BUFSIZE * sizeof(TCHAR);
    LONG lRet;

    lRet = RegOpenKeyEx(
        HKEY_LOCAL_MACHINE,
        TEXT("SYSTEM\\CurrentControlSet\\Control\\ProductOptions"),
        0, KEY_QUERY_VALUE, &hKey);
    if (lRet != ERROR_SUCCESS)
        *succeededP = FALSE;
    else {
        lRet = RegQueryValueEx(hKey, TEXT("ProductType"), NULL, NULL,
                               (LPBYTE) szProductType, &dwBufLen);
        RegCloseKey(hKey);

        if ((lRet != ERROR_SUCCESS) || (dwBufLen > BUFSIZE*sizeof(TCHAR)))
            *succeededP = FALSE;
        else {
            if (lstrcmpi(TEXT("WINNT"), szProductType) == 0)
                pushString(str, "Workstation ");
            else {
                if (lstrcmpi(TEXT("LANMANNT"), szProductType) == 0)
                    pushString(str, "Server ");
                else {
                    if (lstrcmpi(TEXT("SERVERNT"), szProductType) == 0)
                        pushString(str, "Advanced Server ");
                    else
                        pushString(str, "%d.%d ",
                                   osvi.dwMajorVersion, osvi.dwMinorVersion);
                }
            }
            *succeededP = TRUE;
        }
    }
}



static void
displayServicePackBuildNum(OSVERSIONINFOEX osvi,
                           push_string_t * str) {

    // Display service pack (if any) and build number.
    if (osvi.dwMajorVersion == 4 && 
        lstrcmpi(osvi.szCSDVersion, TEXT("Service Pack 6")) == 0) { 

        HKEY hKey;
        LONG lRet;

        // Test for SP6 versus SP6a.
        lRet = RegOpenKeyEx(
            HKEY_LOCAL_MACHINE,
            TEXT("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion"
                 "\\Hotfix\\Q246009"),
            0,
            KEY_QUERY_VALUE,
            &hKey);

        if (lRet == ERROR_SUCCESS)
            pushString(str, "Service Pack 6a (Build %d)\n",
                       osvi.dwBuildNumber & 0xFFFF );         
        else
            // Windows NT 4.0 prior to SP6a
            pushString(str, "%s (Build %d)\n",
                       osvi.szCSDVersion, osvi.dwBuildNumber & 0xFFFF);
        
        RegCloseKey(hKey);
    } else // not Windows NT 4.0 
        pushString(str, "%s (Build %d)\n",
                    osvi.szCSDVersion, osvi.dwBuildNumber & 0xFFFF);
}



static void
getStringVersionNt(OSVERSIONINFOEX osvi,
                   SYSTEM_INFO     si,
                   push_string_t * str,
                   BOOL *          succeededP) {

    // Windows NT product family.

    // Test for the specific product.

    *succeededP = TRUE;  // initial assumption

    if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0) {
        if (osvi.wProductType == VER_NT_WORKSTATION)
            pushString(str, "Windows Vista ");
        else
            pushString(str, "Windows Server \"Longhorn\" ");
    } else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2) {
        if (GetSystemMetrics(SM_SERVERR2))
            pushString(str, "Microsoft Windows Server 2003 \"R2\" ");
        else if (osvi.wProductType == VER_NT_WORKSTATION &&
                 si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64)
            pushString(str,
                       "Microsoft Windows XP Professional x64 Edition ");
        else
            push_string (str, "Microsoft Windows Server 2003, ");
    } else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1)
        pushString(str, "Microsoft Windows XP ");
    else if (osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0)
        pushString(str, "Microsoft Windows 2000 ");
    else if (osvi.dwMajorVersion <= 4)
        pushString(str, "Microsoft Windows NT ");
    else if (bOsVersionInfoEx)
        getStringVersion3(osvi, si, str);
    else
        getStringVersion4(osvi, str, succeededP);

    if (*succeededP)
        displayServicePackBuildNum(str);
}



static void
getStringVersionNt(OSVERSIONINFOEX osvi,
                   push_string_t * str,
                   BOOL *          succeededP) {

    // Windows Me/98/95.

    if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0) {
        pushString(str, "Microsoft Windows 95");
        if (osvi.szCSDVersion[1] == 'C' || osvi.szCSDVersion[1] == 'B')
            pushString(str, " OSR2");
    } else if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 10) {
        pushString(str, "Microsoft Windows 98");
        if (osvi.szCSDVersion[1] == 'A' || osvi.szCSDVersion[1] == 'B')
            pushString(str, " SE");
    } else
        if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 90)
            pushString(str, "Microsoft Windows Millennium Edition\n");

    *succeededP = TRUE;
}



static void
getStringVersion(push_string_t * str,
                 BOOL *          succeededP) {
    
    OSVERSIONINFOEX osvi;
    SYSTEM_INFO si;
    BOOL bOsVersionInfoEx;

    getVersion(&osvi, &si, succeededP);

    if (*succeededP) {
        switch (osvi.dwPlatformId) {
        case VER_PLATFORM_WIN32_NT:
            getStringVersionNt(osvi, si, str, succeededP);
            break;

        case VER_PLATFORM_WIN32_WINDOWS:
            getStringVersionMe9895(osvi, str, succeededP);
            break;

        case VER_PLATFORM_WIN32s:
            pushString(str, "Microsoft Win32s\n");
            *succeededP = TRUE;
            break;
        }
    }
}



const char *
GetHostName(void) {
/*----------------------------------------------------------------------------
   Return the host name of this system.
-----------------------------------------------------------------------------*/
    push_string_t str;
    BOOL succeeded;

    ZeroMemory(&str, sizeof(str));
    get_string_version(&str, &succeeded);
    if (!succeeded) {
#ifndef GETHOSTNAME_LOCAL_DEBUG
        pm_error("Unable to find out host name.");
#endif
        return "unknown";
    } else
        return (const char *)_strdup(str.str);
}



#ifdef GETHOSTNAME_LOCAL_DEBUG
int WINAPI WinMain(HINSTANCE hCurInst, HINSTANCE hPrevInst,
                   LPSTR lpsCmdLine, int nCmdShow) {
    MessageBox(NULL, GetHostName(), TEXT("GetHostName"), MB_OK);

    return 0;
}
#endif

