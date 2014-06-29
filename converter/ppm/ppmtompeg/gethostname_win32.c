/* define this macro for activating debugging version */
/* #define GETHOSTNAME_LOCAL_DEBUG     1*/

#include <windows.h>
#include <tchar.h>
#include <stdarg.h>

#ifndef GETHOSTNAME_LOCAL_DEBUG
#include "pm.h"
#include "gethostname.h"
#endif

#define BUFSIZE 80

typedef void (WINAPI *PGNSI)(LPSYSTEM_INFO);
typedef BOOL (WINAPI *PGPI)(DWORD, DWORD, DWORD, DWORD, PDWORD);

typedef struct {
    char  str[256];
    int   level;
} push_string_t;

static void
pushString(push_string_t *p, const char *fmt, ...)
{
    va_list args;

    va_start(args, fmt);
    p->level += _vsnprintf(p->str + p->level, sizeof(p->str)-p->level, fmt, args);
    va_end(args);
}

#if _WIN32_WINNT < 0x0600
/*
 * Reference available here:
 *
 * GetProductInfo() Function
 * http://msdn2.microsoft.com/en-us/library/ms724358.aspx
 */
#define PRODUCT_BUSINESS                        0x00000006  /* Business Edition */
#define PRODUCT_BUSINESS_N                      0x00000010  /* Business Edition */
#define PRODUCT_CLUSTER_SERVER                  0x00000012  /* Cluster Server Edition */
#define PRODUCT_DATACENTER_SERVER               0x00000008  /* Server Datacenter Edition (full installation) */
#define PRODUCT_DATACENTER_SERVER_CORE          0x0000000C  /* Server Datacenter Edition (core installation) */
#define PRODUCT_ENTERPRISE                      0x00000004  /* Enterprise Edition */
#define PRODUCT_ENTERPRISE_N                    0x0000001B  /* Enterprise Edition */
#define PRODUCT_ENTERPRISE_SERVER               0x0000000A  /* Server Enterprise Edition (full installation) */
#define PRODUCT_ENTERPRISE_SERVER_CORE          0x0000000E  /* Server Enterprise Edition (core installation) */
#define PRODUCT_ENTERPRISE_SERVER_IA64          0x0000000F  /* Server Enterprise Edition for Itanium-based Systems */
#define PRODUCT_HOME_BASIC                      0x00000002  /* Home Basic Edition */
#define PRODUCT_HOME_BASIC_N                    0x00000005  /* Home Basic Edition */
#define PRODUCT_HOME_PREMIUM                    0x00000003  /* Home Premium Edition */
#define PRODUCT_HOME_PREMIUM_N                  0x0000001A  /* Home Premium Edition */
#define PRODUCT_HOME_SERVER                     0x00000013  /* Home Server Edition */
#define PRODUCT_SERVER_FOR_SMALLBUSINESS        0x00000018  /* Server for Small Business Edition */
#define PRODUCT_SMALLBUSINESS_SERVER            0x00000009  /* Small Business Server */
#define PRODUCT_SMALLBUSINESS_SERVER_PREMIUM    0x00000019  /* Small Business Server Premium Edition */
#define PRODUCT_STANDARD_SERVER                 0x00000007  /* Server Standard Edition (full installation) */
#define PRODUCT_STANDARD_SERVER_CORE            0x0000000D  /* Server Standard Edition (core installation) */
#define PRODUCT_STARTER                         0x0000000B  /* Starter Edition */
#define PRODUCT_STORAGE_ENTERPRISE_SERVER       0x00000017  /* Storage Server Enterprise Edition */
#define PRODUCT_STORAGE_EXPRESS_SERVER          0x00000014  /* Storage Server Express Edition */
#define PRODUCT_STORAGE_STANDARD_SERVER         0x00000015  /* Storage Server Standard Edition */
#define PRODUCT_STORAGE_WORKGROUP_SERVER        0x00000016  /* Storage Server Workgroup Edition */
#define PRODUCT_UNDEFINED                       0x00000000  /* An unknown product */
#define PRODUCT_ULTIMATE                        0x00000001  /* Ultimate Edition */
#define PRODUCT_ULTIMATE_N                      0x0000001C  /* Ultimate Edition */
#define PRODUCT_WEB_SERVER                      0x00000011  /* Web Server Edition (full installation) */
#define PRODUCT_WEB_SERVER_CORE                 0x0000001D  /* Web Server Edition (core installation) */
#endif

static BOOL
get_string_version(push_string_t *str)
{
    OSVERSIONINFOEX osvi;
    SYSTEM_INFO si;
    PGPI pGPI;
    PGNSI pGNSI;
    BOOL bOsVersionInfoEx;
    DWORD dwType;

    ZeroMemory(&si, sizeof(SYSTEM_INFO));
    ZeroMemory(&osvi, sizeof(OSVERSIONINFOEX));

    /*
     * Try calling GetVersionEx using the OSVERSIONINFOEX structure.
     * If that fails, try using the OSVERSIONINFO structure.
     */
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFOEX);
    if( !(bOsVersionInfoEx = GetVersionEx ((OSVERSIONINFO *) &osvi)) )
    {
        osvi.dwOSVersionInfoSize = sizeof (OSVERSIONINFO);
        if (! GetVersionEx ( (OSVERSIONINFO *) &osvi) ) 
            return FALSE;
    }

    /* Call GetNativeSystemInfo if available; GetSystemInfo otherwise. */
    pGNSI = (PGNSI)
            GetProcAddress(GetModuleHandle(TEXT("kernel32.dll")), 
            "GetNativeSystemInfo");
    if (NULL != pGNSI)
        pGNSI(&si);
    else
        GetSystemInfo(&si);

    switch (osvi.dwPlatformId)
    {
    /* Test for the Windows NT product family. */
    case VER_PLATFORM_WIN32_NT:
        /* Test for the specific product. */
        if (osvi.dwMajorVersion == 6 && osvi.dwMinorVersion == 0)
        {
            if (osvi.wProductType == VER_NT_WORKSTATION)
                pushString(str, "Windows Vista ");
            else
                pushString(str, "Windows Server 2008 ");

            pGPI = (PGPI) GetProcAddress(
                GetModuleHandle(TEXT("kernel32.dll")),
                "GetProductInfo");

            pGPI( 6, 0, 0, 0, &dwType);
            switch (dwType)
            {
            case PRODUCT_ULTIMATE:
                pushString(str, "Ultimate Edition");
                break;
            case PRODUCT_HOME_PREMIUM:
                pushString(str, "Home Premium Edition");
                break;
            case PRODUCT_HOME_BASIC:
                pushString(str, "Home Basic Edition");
                break;
            case PRODUCT_ENTERPRISE:
                pushString(str, "Enterprise Edition");
                break;
            case PRODUCT_BUSINESS:
                pushString(str, "Business Edition");
                break;
            case PRODUCT_STARTER:
                pushString(str, "Starter Edition");
                break;
            case PRODUCT_CLUSTER_SERVER:
                pushString(str, "Cluster Server Edition");
                break;
            case PRODUCT_DATACENTER_SERVER:
                pushString(str, "Datacenter Edition");
                break;
            case PRODUCT_DATACENTER_SERVER_CORE:
                pushString(str, "Datacenter Edition (core installation)");
                break;
            case PRODUCT_ENTERPRISE_SERVER:
                pushString(str, "Enterprise Edition");
                break;
            case PRODUCT_ENTERPRISE_SERVER_CORE:
                pushString(str, "Enterprise Edition (core installation)");
                break;
            case PRODUCT_ENTERPRISE_SERVER_IA64:
                pushString(str, "Enterprise Edition for Itanium-based Systems");
                break;
            case PRODUCT_SMALLBUSINESS_SERVER:
                pushString(str, "Small Business Server");
                break;
            case PRODUCT_SMALLBUSINESS_SERVER_PREMIUM:
                pushString(str, "Small Business Server Premium Edition");
                break;
            case PRODUCT_STANDARD_SERVER:
                pushString(str, "Standard Edition");
                break;
            case PRODUCT_STANDARD_SERVER_CORE:
                pushString(str, "Standard Edition (core installation)");
                break;
            case PRODUCT_WEB_SERVER:
                pushString(str, "Web Server Edition");
                break;
            }
            if (si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64)
                pushString(str,  ", 64-bit");
            else
            if (si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_INTEL)
                pushString(str, ", 32-bit");
            else
                /* space for optional build number */
                pushString(str, " ");
        }
        else
        if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 2 )
        {
            if( GetSystemMetrics(SM_SERVERR2) )
                pushString(str, "Microsoft Windows Server 2003 \"R2\" ");
            else
            if ( osvi.wSuiteMask==VER_SUITE_STORAGE_SERVER )
                pushString(str, "Windows Storage Server 2003 ");
            else
            if( osvi.wProductType == VER_NT_WORKSTATION &&
                si.wProcessorArchitecture==PROCESSOR_ARCHITECTURE_AMD64)
                pushString(str, "Microsoft Windows XP Professional x64 Edition ");
            else
                pushString(str, "Microsoft Windows Server 2003, ");

            /* Test for the server type. */
            if ( osvi.wProductType != VER_NT_WORKSTATION )
            {
                switch (si.wProcessorArchitecture)
                {
                case PROCESSOR_ARCHITECTURE_IA64:
                    if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
                       pushString(str, "Datacenter Edition for Itanium-based Systems ");
                    else
                    if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                       pushString(str, "Enterprise Edition for Itanium-based Systems ");
                    break;

                case PROCESSOR_ARCHITECTURE_AMD64:
                    if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
                        pushString(str, "Datacenter x64 Edition ");
                    else
                    if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                        pushString(str, "Enterprise x64 Edition ");
                    else
                        pushString(str, "Standard x64 Edition ");
                    break;

                default:
                    if ( osvi.wSuiteMask & VER_SUITE_COMPUTE_SERVER)
                        pushString(str, "Compute Cluster Edition ");
                    else
                    if ( osvi.wSuiteMask & VER_SUITE_DATACENTER)
                        pushString(str, "Datacenter Edition ");
                    else
                    if ( osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                        pushString(str, "Enterprise Edition ");
                    else
                    if ( osvi.wSuiteMask & VER_SUITE_BLADE)
                        pushString(str, "Web Edition ");
                    else
                        pushString(str, "Standard Edition ");
                    break;
                }
            }
        }
        else
        if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 1 )
        {
            pushString(str, "Microsoft Windows XP ");
            if( osvi.wSuiteMask & VER_SUITE_PERSONAL )
                pushString(str, "Home Edition ");
            else
                pushString(str, "Professional ");
        }
        else
        if ( osvi.dwMajorVersion == 5 && osvi.dwMinorVersion == 0 )
        {
            pushString(str, "Microsoft Windows 2000 ");
            if (osvi.wProductType == VER_NT_WORKSTATION)
                pushString(str, "Professional ");
            else 
            if (osvi.wSuiteMask & VER_SUITE_DATACENTER)
                pushString(str, "Datacenter Server ");
            else
            if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                pushString(str, "Advanced Server ");
            else
                pushString(str, "Server ");
        } else
        if ( osvi.dwMajorVersion <= 4 )
            pushString(str, "Microsoft Windows NT ");

        /* Test for specific product on Windows NT 4.0 SP6 and later. */
        if (bOsVersionInfoEx)
        {
            /* Test for the workstation type. */
            switch (osvi.wProductType)
            {
            case VER_NT_WORKSTATION:
                if (si.wProcessorArchitecture!=PROCESSOR_ARCHITECTURE_AMD64 &&
                    osvi.dwMajorVersion == 4)
                    pushString(str, "Workstation 4.0 ");
                break;

            case VER_NT_SERVER:
            case VER_NT_DOMAIN_CONTROLLER:
                if (osvi.wSuiteMask & VER_SUITE_ENTERPRISE)
                    pushString(str, "Server 4.0, Enterprise Edition ");
                else
                    pushString(str, "Server 4.0 ");
                break;
            }
        }
        /* Test for specific product on Windows NT 4.0 SP5 and earlier */
        else  
        {
            HKEY hKey;
            TCHAR szProductType[BUFSIZE];
            DWORD dwBufLen=BUFSIZE*sizeof(TCHAR);
            LONG lRet;

            lRet = RegOpenKeyEx(HKEY_LOCAL_MACHINE,
                TEXT("SYSTEM\\CurrentControlSet\\Control\\ProductOptions"),
                0, KEY_QUERY_VALUE, &hKey);
            if (lRet != ERROR_SUCCESS)
                return FALSE;

            lRet = RegQueryValueEx(hKey, TEXT("ProductType"), NULL, NULL,
                (LPBYTE) szProductType, &dwBufLen);
            RegCloseKey( hKey );

            if ((lRet != ERROR_SUCCESS) || (dwBufLen > BUFSIZE*sizeof(TCHAR)))
                return FALSE;

            if (lstrcmpi(TEXT("WINNT"), szProductType) == 0)
                pushString(str, "Workstation ");
            else
            if (lstrcmpi(TEXT("LANMANNT"), szProductType) == 0)
                pushString(str, "Server ");
            else
            if (lstrcmpi( TEXT("SERVERNT"), szProductType) == 0)
                pushString(str, "Advanced Server ");
            else
                pushString(str, "%d.%d ", osvi.dwMajorVersion, osvi.dwMinorVersion);
        }

        /* Display service pack (if any) and build number. */
        if (osvi.dwMajorVersion == 4 && 
            lstrcmpi(osvi.szCSDVersion, TEXT("Service Pack 6")) == 0)
        { 
            HKEY hKey;
            LONG lRet;

            /* Test for SP6 versus SP6a. */
            lRet = RegOpenKeyEx( HKEY_LOCAL_MACHINE,
                TEXT("SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Hotfix\\Q246009"),
                0, KEY_QUERY_VALUE, &hKey );
            if( lRet == ERROR_SUCCESS )
                pushString(str, "Service Pack 6a (Build %d)\n", osvi.dwBuildNumber & 0xFFFF );         
            else
                /* Windows NT 4.0 prior to SP6a */
                pushString(str, "%s (Build %d)\n", osvi.szCSDVersion, osvi.dwBuildNumber & 0xFFFF);

             RegCloseKey( hKey );
        }
        else /* not Windows NT 4.0 */
            pushString(str, "%s (Build %d)\n", osvi.szCSDVersion, osvi.dwBuildNumber & 0xFFFF);

        break;

    /* Test for the Windows Me/98/95. */
    case VER_PLATFORM_WIN32_WINDOWS:
        if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 0)
        {
            pushString(str, "Microsoft Windows 95");
            if (osvi.szCSDVersion[1]=='C' || osvi.szCSDVersion[1]=='B')
                pushString(str, " OSR2");
        }
        else
        if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 10)
        {
            pushString(str, "Microsoft Windows 98");
            if (osvi.szCSDVersion[1]=='A' || osvi.szCSDVersion[1]=='B')
                pushString(str, " SE");
        } 
        else
        if (osvi.dwMajorVersion == 4 && osvi.dwMinorVersion == 90)
            pushString(str, "Microsoft Windows Millennium Edition\n");
        break;

    case VER_PLATFORM_WIN32s:
        pushString(str, "Microsoft Win32s\n");
        break;
    }
    return TRUE; 
}

const char *
GetHostName(void)
{
/*----------------------------------------------------------------------------
   Return the host name of this system.
-----------------------------------------------------------------------------*/
    push_string_t str;

    ZeroMemory(&str, sizeof(str));
    if (!get_string_version(&str)) {
#ifndef GETHOSTNAME_LOCAL_DEBUG
        pm_error("Unable to find out host name.");
#endif
        pushString(&str, "unknown");
    }
    return (const char *)_strdup(str.str);
}

#ifdef GETHOSTNAME_LOCAL_DEBUG
int WINAPI WinMain(HINSTANCE hCurInst, HINSTANCE hPrevInst,
                   LPSTR lpsCmdLine, int nCmdShow)
{
    char *hostName = (char *)GetHostName();

    /* compile as ascii only (not UNICODE) */
    MessageBox(NULL, hostName, TEXT("GetHostName"), MB_OK);
    free(hostName);

    return 0;
}
#endif
