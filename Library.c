#pragma warning( push, 1 )
#define CINTERFACE
#define COBJMACROS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0600
#define _WIN32_IE WINVER
#define _WIN32_WINNT WINVER
#include <windows.h>
#include <mmsystem.h>
#include <vfw.h>
#include <shlwapi.h>
#define extern __declspec( dllimport )
#include <ddraw.h>
#undef extern
#include <dbghelp.h>
#pragma warning( pop )
#include "PodHacks.h"

HRESULT CALLBACK PODHACKS_DllGetVersion( DLLVERSIONINFO2 * pdvi )
{
    if( pdvi )
    {
        switch( pdvi->info1.cbSize )
        {
        case sizeof(DLLVERSIONINFO2):
            pdvi->dwFlags = 0;
            pdvi->ullVersion = MAKEDLLVERULL( PODHACKS_VER_MAJOR, PODHACKS_VER_MINOR, PODHACKS_VER_PATCH, 0 );
            /* fall-through */

        case sizeof(DLLVERSIONINFO):
            pdvi->info1.dwMajorVersion = PODHACKS_VER_MAJOR;
            pdvi->info1.dwMinorVersion = PODHACKS_VER_MINOR;
            pdvi->info1.dwBuildNumber = PODHACKS_VER_PATCH;
            pdvi->info1.dwPlatformID = DLLVER_PLATFORM_WINDOWS;
            return S_OK;
        }
    }
    return E_INVALIDARG;
}

HMODULE g_AppModule;
CHAR g_AppDirectory[ MAX_PATH ];
CHAR g_CDVolumeName[ MAX_PATH ];

void FullAppFileName( LPSTR FullPath, LPCSTR FileName )
{
    lstrcpynA( FullPath, g_AppDirectory, MAX_PATH - lstrlenA( FileName ) );
    lstrcatA( FullPath, FileName );
}

BOOL FileExists( LPCSTR FileName )
{
    WIN32_FIND_DATAA FindData;
    HANDLE FindFile = FindFirstFileA( FileName, &FindData );
    if( FindFile != INVALID_HANDLE_VALUE )
    {
        FindClose( FindFile );
        return TRUE;
    }
    return FALSE;
}

BOOL AppFileExists( LPCSTR FileName )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, FileName );
    return FileExists( FullPath );
}

void ExtractFileName( LPCSTR FileName, LPSTR FullPath, LPSTR * FilePart )
{
    lstrcpynA( FullPath, FileName, MAX_PATH );
    for( *FilePart = &FullPath[ lstrlenA( FullPath ) ]; *FilePart > FullPath; --(*FilePart) )
        if( ('\\' == (*FilePart)[ -1 ]) || ('/' == (*FilePart)[ -1 ]) )
            break;
}

BOOL ChangeFileNameIf( LPSTR FullPath, LPCSTR FileName, LPCSTR Compare, LPCSTR Replace )
{
    LPSTR FilePart;
    ExtractFileName( FileName, FullPath, &FilePart );
    if( 0 == lstrcmpiA( FilePart, Compare ) )
    {
        lstrcpynA( FilePart, Replace, MAX_PATH - (FilePart - FullPath) );
        return TRUE;
    }
    return FALSE;
}

/************************************************************************/
/* Redirect Data\Binary\Video\IntroCD.avi to IntroPOD.avi               */
/************************************************************************/

STDAPI AVIFIL32_AVIFileOpenA( PAVIFILE * ppfile, LPCSTR szFile, UINT uMode, LPCLSID lpHandler )
{
    if( FALSE == FileExists( szFile ) )
    {
        CHAR FullPath[ MAX_PATH ];
        if( ChangeFileNameIf( FullPath, szFile, "IntroCD.avi", "IntroPOD.avi" ) )
            return AVIFileOpenA( ppfile, FullPath, uMode, lpHandler );
    }
    return AVIFileOpenA( ppfile, szFile, uMode, lpHandler );
}

/************************************************************************/
/* Write mini dump in the appication directory on unhandled exceptions  */
/************************************************************************/

CHAR g_MiniDumpFileName[ MAX_PATH + sizeof("PodHacks_YYYYmmddHHiiss.dmp") ];

void InitMiniDumpFileName( void )
{
    SYSTEMTIME SystemTime;
    CHAR FileName[ MAX_PATH ];
    GetSystemTime( &SystemTime );
    wsprintfA( FileName, "PodHacks_%04hu%02hu%02hu%02hu%02hu%02hu.dmp",
        SystemTime.wYear, SystemTime.wMonth, SystemTime.wDay,
        SystemTime.wHour, SystemTime.wMinute, SystemTime.wSecond );
    lstrcpyA( g_MiniDumpFileName, g_AppDirectory );
    lstrcatA( g_MiniDumpFileName, FileName );
}

typedef BOOL (WINAPI * PMINIDUMPWRITEDUMP)( HANDLE, DWORD, HANDLE, MINIDUMP_TYPE, PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION, PMINIDUMP_CALLBACK_INFORMATION );
PMINIDUMPWRITEDUMP g_MiniDumpWriteDump;

BOOL InitMiniDumpAPI( void )
{
    if( NULL == g_MiniDumpWriteDump )
    {
        HMODULE Module = LoadLibraryA( "dbghelp.dll" );
        if( Module )
        {
            g_MiniDumpWriteDump = (PMINIDUMPWRITEDUMP)GetProcAddress( Module, "MiniDumpWriteDump" );
            if( NULL == g_MiniDumpWriteDump )
                FreeLibrary( Module );
        }
    }
    return g_MiniDumpWriteDump ? TRUE : FALSE;
}

MINIDUMP_TYPE g_MiniDumpType;

DWORD WINAPI SaveMiniDump( LPVOID lpThreadParameter )
{
    PMINIDUMP_EXCEPTION_INFORMATION ExceptionParam = lpThreadParameter;
    if( InitMiniDumpAPI() )
    {
        HANDLE File = CreateFileA( g_MiniDumpFileName, GENERIC_WRITE | GENERIC_READ, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
        if( File != INVALID_HANDLE_VALUE )
        {
            BOOL Saved = g_MiniDumpWriteDump( GetCurrentProcess(), GetCurrentProcessId(), File, g_MiniDumpType, ExceptionParam, NULL, NULL );
            CloseHandle( File );
            if( !Saved )
                DeleteFileA( g_MiniDumpFileName );
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL g_MiniDumpEnabled;

LONG WINAPI TopLevelExceptionFilter( PEXCEPTION_POINTERS ExceptionInfo )
{
    if( g_MiniDumpEnabled )
    {
        MINIDUMP_EXCEPTION_INFORMATION ExceptionParam;
        ExceptionParam.ThreadId = GetCurrentThreadId();
        ExceptionParam.ExceptionPointers = ExceptionInfo;
        ExceptionParam.ClientPointers = FALSE;
        if( ExceptionInfo && ExceptionInfo->ExceptionRecord &&
            EXCEPTION_STACK_OVERFLOW == ExceptionInfo->ExceptionRecord->ExceptionCode )
        {
            DWORD ThreadId;
            HANDLE Thread = CreateThread( NULL, 0, SaveMiniDump, &ExceptionParam, 0, &ThreadId );
            if( Thread )
            {
                DWORD ExitCode;
                WaitForSingleObject( Thread, INFINITE );
                if( !GetExitCodeThread( Thread, &ExitCode ) )
                    ExitCode = EXCEPTION_CONTINUE_SEARCH;
                CloseHandle( Thread );
                return ExitCode;
            }
        }
        return SaveMiniDump( &ExceptionParam );
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

LPTOP_LEVEL_EXCEPTION_FILTER WINAPI KERNEL32_SetUnhandledExceptionFilter( LPTOP_LEVEL_EXCEPTION_FILTER lpTopLevelExceptionFilter )
{
    if( g_MiniDumpEnabled )
        return NULL;
    return SetUnhandledExceptionFilter( lpTopLevelExceptionFilter );
}

/************************************************************************/
/* Capture display lock and main thread id                              */
/************************************************************************/

HANDLE g_DisplayLock;
DWORD g_Main_ThreadId;

HANDLE WINAPI KERNEL32_CreateSemaphoreA( LPSECURITY_ATTRIBUTES lpSemaphoreAttributes, LONG lInitialCount, LONG lMaximumCount, LPCSTR lpName )
{
    HANDLE Semaphore = CreateSemaphoreA( lpSemaphoreAttributes, lInitialCount, lMaximumCount, lpName );
    if( Semaphore && lpName && (0 == lstrcmpiA( lpName, "SEM_AFFICHAGE" )) && (1 == lMaximumCount) )
    {
        g_DisplayLock = Semaphore;
        g_Main_ThreadId = GetCurrentThreadId();
        if( g_MiniDumpEnabled )
            g_MiniDumpEnabled = InitMiniDumpAPI();
    }
    return Semaphore;
}


/************************************************************************/
/* Avoid deadlock in POD's Sound Engine thread                          */
/************************************************************************/

DWORD g_SoundEngine_ThreadId;

MMRESULT WINAPI WINMM_waveOutGetDevCapsA( UINT_PTR uDeviceID, LPWAVEOUTCAPSA pwoc, UINT cbwoc )
{
    g_SoundEngine_ThreadId = GetCurrentThreadId();
    return waveOutGetDevCapsA( uDeviceID, pwoc, cbwoc );
}

LONG volatile g_DisplayHacks;

DWORD WINAPI KERNEL32_WaitForSingleObject( HANDLE hHandle, DWORD dwMilliseconds )
{
    if( g_DisplayLock && (g_DisplayLock == hHandle) )
    {
        if( GetCurrentThreadId() == g_SoundEngine_ThreadId )
        {
            DWORD State = WaitForSingleObject( hHandle, 0 );
            if( WAIT_TIMEOUT == State )
            {
                State = WAIT_OBJECT_0;
                InterlockedIncrement( &g_DisplayHacks );
            }
            return State;
        }
    }
    return WaitForSingleObject( hHandle, dwMilliseconds );
}

BOOL WINAPI KERNEL32_ReleaseSemaphore( HANDLE hSemaphore, LONG lReleaseCount, LPLONG lpPreviousCount )
{
    if( g_DisplayLock && (g_DisplayLock == hSemaphore) && (GetCurrentThreadId() == g_SoundEngine_ThreadId) )
    {
        if( InterlockedDecrement( &g_DisplayHacks ) < 0 )
            InterlockedIncrement( &g_DisplayHacks );
        else
        {
            if( lpPreviousCount )
                *lpPreviousCount = 1;
            return TRUE;
        }
    }
    return ReleaseSemaphore( hSemaphore, lReleaseCount, lpPreviousCount );
}

/************************************************************************/
/* Workaround for Wine http://bugs.winehq.org/show_bug.cgi?id=31924     */
/************************************************************************/

MMRESULT WINAPI WINMM_mixerGetDevCapsA( UINT_PTR uMxId, LPMIXERCAPSA pmxcaps, UINT cbmxcaps )
{
    MMRESULT Result = mixerGetDevCapsA( uMxId, pmxcaps, cbmxcaps );
    if( (MMSYSERR_BADDEVICEID == Result) && (mixerGetNumDevs() <= uMxId) )
    {
        UINT MixerId = 0;
        if( MMSYSERR_NOERROR == mixerGetID( (HMIXEROBJ)uMxId, &MixerId, MIXER_OBJECTF_HMIXER ) )
            Result = mixerGetDevCapsA( MixerId, pmxcaps, cbmxcaps );
    }
    return Result;
}

/************************************************************************/
/* CreateBitmap fix for the D3D5 version                                */
/************************************************************************/

HBITMAP WINAPI GDI32_CreateBitmap( int nWidth, int nHeight, UINT nPlanes, UINT nBitCount, CONST VOID * lpBits )
{
    /*
     * NOTE: The game passes nPlanes = 3 (this would indicate a composite/planar bitmap).
     * The size of the lpBits data would be nWidth * nHeight * nPlanes * nBitCount bits!
     * Infact it's a standard interlaved 5:6:5 bitmap (nWidth * nHeight * nPlanes * 16).
     * Luckely, the API dismisses the request and doesn't access the bitmap data memory.
     */
    if( (3 == nPlanes) && (16 == nBitCount) )
        nPlanes = 1;
    return CreateBitmap( nWidth, nHeight, nPlanes, nBitCount, lpBits );
}

/************************************************************************/
/* Override desktop color depth                                         */
/************************************************************************/

int WINAPI GDI32_GetDeviceCaps( HDC hdc, int index )
{
    if( BITSPIXEL == index )
        return 16;
    return GetDeviceCaps( hdc, index );
}

/************************************************************************/
/* Redirect CD-ROM data access to application directory                 */
/************************************************************************/

UINT WINAPI KERNEL32_GetDriveTypeA( LPCSTR lpRootPathName )
{
    MEMORY_BASIC_INFORMATION MemInfo;
    if( (0 == lstrcmpiA( lpRootPathName, "A:\\" )) &&
        (sizeof(MEMORY_BASIC_INFORMATION) == VirtualQuery( lpRootPathName, &MemInfo, sizeof(MEMORY_BASIC_INFORMATION) )) &&
        (g_AppModule == MemInfo.AllocationBase) &&
        (((PAGE_READWRITE | PAGE_WRITECOPY | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY) & MemInfo.Protect) != 0) )
    {
        lstrcpynA( (LPSTR)lpRootPathName, ".\\", 8 );
        return DRIVE_CDROM;
    }
    return GetDriveTypeA( lpRootPathName );
}

BOOL WINAPI KERNEL32_GetVolumeInformationA( LPCSTR lpRootPathName, LPSTR lpVolumeNameBuffer, DWORD nVolumeNameSize, LPDWORD lpVolumeSerialNumber, LPDWORD lpMaximumComponentLength, LPDWORD lpFileSystemFlags, LPSTR lpFileSystemNameBuffer, DWORD nFileSystemNameSize )
{
    if( 0 == lstrcmpiA( lpRootPathName, ".\\" ) )
    {
        lstrcpynA( lpVolumeNameBuffer, g_CDVolumeName, nVolumeNameSize );
        if( lpVolumeSerialNumber )
            *lpVolumeSerialNumber = 'XDOP';
        if( lpMaximumComponentLength )
            *lpMaximumComponentLength = 110;
        if( lpFileSystemFlags )
            *lpFileSystemFlags = FILE_READ_ONLY_VOLUME | FILE_UNICODE_ON_DISK | FILE_CASE_SENSITIVE_SEARCH;
        lstrcpynA( lpFileSystemNameBuffer, "CDFS", nFileSystemNameSize );
        return TRUE;
    }
    return GetVolumeInformationA( lpRootPathName, lpVolumeNameBuffer, nVolumeNameSize, lpVolumeSerialNumber, lpMaximumComponentLength, lpFileSystemFlags, lpFileSystemNameBuffer, nFileSystemNameSize );
}

/************************************************************************/
/* Support for OEM CD-ROM checks and CR-ROM redirection                 */
/************************************************************************/

BOOL g_RedirectCDR;

HANDLE g_LastFile;
DWORD  g_LastSize;

HANDLE WINAPI KERNEL32_CreateFileA( LPCSTR lpFileName, DWORD dwDesiredAccess, DWORD dwShareMode, LPSECURITY_ATTRIBUTES lpSecurityAttributes, DWORD dwCreationDisposition, DWORD dwFlagsAndAttributes, HANDLE hTemplateFile )
{
    HANDLE File;
    CHAR FullPath[ MAX_PATH ];
    if( g_RedirectCDR )
    {
        LPSTR FilePart;
        ExtractFileName( lpFileName, FullPath, &FilePart );
        if( 0 == lstrcmpiA( FilePart, "Pod.tmp" ) )
        {
            SetLastError( ERROR_ACCESS_DENIED );
            return INVALID_HANDLE_VALUE;
        }
    }
    File = CreateFileA( lpFileName, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile );
    /* the OEM version opens EXE files on the CD-ROM exclusively for the GetFileTime check */
    if( (INVALID_HANDLE_VALUE == File) && (GENERIC_READ == dwDesiredAccess) && (0 == dwShareMode) && (OPEN_EXISTING == dwCreationDisposition) )
        File = CreateFileA( lpFileName, dwDesiredAccess, FILE_SHARE_READ, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile );
    if( g_RedirectCDR )
    {
        g_LastFile = File;
        if( File != INVALID_HANDLE_VALUE )
        {
            g_LastSize = GetFileSize( File, NULL );
        }
        else
        {
            g_LastSize = INVALID_FILE_SIZE;
            if( ChangeFileNameIf( FullPath, lpFileName, "IntroCD.avi", "IntroPOD.avi" ) )
            {
                File = CreateFileA( FullPath, dwDesiredAccess, dwShareMode, lpSecurityAttributes, dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile );
                g_LastFile = File;
                if( File != INVALID_HANDLE_VALUE )
                    g_LastSize = 81317196;
            }
            if( (INVALID_HANDLE_VALUE == File) && lpFileName && ('.' == lpFileName[ 0 ]) && ('\\' == lpFileName[ 1 ]) )
            {
                lstrcpynA( FullPath, &lpFileName[ 2 ], sizeof("UbiSoft\\") );
                if( lstrcmpiA( FullPath, "UbiSoft\\" ) != 0 )
                {
                    /* the OEM version opens EXE files on the CD-ROM exclusively for the GetFileTime check */
                    if( (GENERIC_READ == dwDesiredAccess) && (0 == dwShareMode) && (OPEN_EXISTING == dwCreationDisposition) )
                        return NULL;
                    FullAppFileName( FullPath, "PodHacks.log" );
                    {
                        HANDLE Log = CreateFileA( FullPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
                        if( Log != INVALID_HANDLE_VALUE )
                        {
                            CHAR Line[ 1024 ];
                            lstrcpynA( Line, "CD: ", 1024 );
                            lstrcpynA( &Line[ lstrlenA( Line ) ], &lpFileName[ 2 ], 1024 - lstrlenA( Line ) - 2 );
                            lstrcatA( Line, "\r\n" );
                            SetFilePointer( Log, 0, NULL, FILE_END );
                            {
                                DWORD Written;
                                WriteFile( Log, Line, lstrlenA(Line), &Written, NULL );
                            }
                            CloseHandle( Log );
                        }
                    }
                }
            }
        }
    }
    return File;
}

DWORD WINAPI KERNEL32_GetFileSize( HANDLE hFile, LPDWORD lpFileSizeHigh )
{
    if( g_LastFile == hFile )
    {
        if( lpFileSizeHigh )
            *lpFileSizeHigh = 0;
        return g_LastSize;
    }
    return GetFileSize( hFile, lpFileSizeHigh );
}

BOOL WINAPI KERNEL32_GetFileTime( HANDLE hFile, LPFILETIME lpCreationTime, LPFILETIME lpLastAccessTime, LPFILETIME lpLastWriteTime )
{
    BOOL Result;
    if( (hFile != NULL) && (hFile != INVALID_HANDLE_VALUE) )
    {
        Result = GetFileTime( hFile, lpCreationTime, lpLastAccessTime, lpLastWriteTime );
    }
    else
    {
        SetLastError( ERROR_INVALID_HANDLE );
        Result = FALSE;
    }
    if( lpLastAccessTime )
    {
        lpLastAccessTime->dwLowDateTime = 0;
        lpLastAccessTime->dwHighDateTime = 0;
    }
    return Result;
}

BOOL WINAPI KERNEL32_GetDiskFreeSpaceA( LPCSTR lpRootPathName, LPDWORD lpSectorsPerCluster, LPDWORD lpBytesPerSector, LPDWORD lpNumberOfFreeClusters, LPDWORD lpTotalNumberOfClusters )
{
    if( g_RedirectCDR && (0 == lstrcmpiA( lpRootPathName, ".\\" )) )
    {
        if( lpSectorsPerCluster )
            *lpSectorsPerCluster = 16;
        if( lpBytesPerSector )
            *lpBytesPerSector = 2048;
        if( lpNumberOfFreeClusters )
            *lpNumberOfFreeClusters = 0;
        if( lpTotalNumberOfClusters )
            *lpTotalNumberOfClusters = 'XDOP';
        return TRUE;
    }
    {
        BOOL Result = GetDiskFreeSpaceA( lpRootPathName, lpSectorsPerCluster, lpBytesPerSector, lpNumberOfFreeClusters, lpTotalNumberOfClusters );
        if( Result && (DRIVE_CDROM == GetDriveTypeA( lpRootPathName )) )
        {
            /* support directories mapped as CD-ROM (Wine) */
            if( lpNumberOfFreeClusters )
                *lpNumberOfFreeClusters = 0;
            /* the OEM version checks the two obsolete values - not even included in the Windows 95 appcompat shim */
            if( lpSectorsPerCluster )
                *lpSectorsPerCluster = 16;
            if( lpBytesPerSector )
                *lpBytesPerSector = 2048;
        }
        return Result;
    }
}

/************************************************************************/
/* Redirect %WinDir% to application directory (UbiSoft\Ubi.ini)         */
/************************************************************************/

UINT WINAPI KERNEL32_GetWindowsDirectoryA( LPSTR lpBuffer, UINT uSize )
{
    CHAR WinDir[ MAX_PATH ];
    lstrcpynA( WinDir, g_AppDirectory, MAX_PATH );
    {
        LPSTR End = &WinDir[ lstrlenA( WinDir ) ];
        while( (End > WinDir) && (('\\' == End[ -1 ]) || ('/' == End[ -1 ])) )
            --End;
        *End = '\0';
    }
    {
        UINT Length = lstrlenA( WinDir );
        if( Length >= uSize )
            Length++;
        lstrcpynA( lpBuffer, WinDir, uSize );
        return Length;
    }
}

/************************************************************************/
/* Redirect CD-ROM audio access to Track02.wav or Track02.mp3           */
/************************************************************************/

HWND g_PlayerWindow;
HANDLE g_PlayerLock;
struct PlayerMessages {
    UINT Find;
    UINT Open;
    UINT SetTimeFormat;
    UINT GetItemLength;
    UINT SetCallback;
    UINT Play;
    UINT Stop;
    UINT Close;
    UINT Pause;
    UINT Resume;
    UINT GetVolume;
    UINT SetVolume;
} g_PlayerMessage;

BOOL CALLBACK Player_Find( HWND Wnd, LPARAM Param )
{
    UNREFERENCED_PARAMETER( Param );
    if( (DWORD)MAKELONG( PODHACKS_VER_MINOR, PODHACKS_VER_MAJOR ) == (DWORD)SendMessageA( Wnd, g_PlayerMessage.Find, 0, 0 ) )
    {
        g_PlayerWindow = Wnd;
        return FALSE;
    }
    return TRUE;
}

MCIERROR Player_Init( void )
{
    if( g_PlayerWindow )
        return MMSYSERR_NOERROR;
    g_PlayerLock = CreateMutexA( 0, TRUE, "PodHacks_Player" );
    if( 0 == g_PlayerLock )
        return MCIERR_DEVICE_LOCKED;
    if( ERROR_ALREADY_EXISTS == GetLastError() )
    {
        CloseHandle( g_PlayerLock );
        g_PlayerLock = 0;
        return MCIERR_DEVICE_LOCKED;
    }
    g_PlayerMessage.Find          = RegisterWindowMessageA( "PodHacks_Find" );
    g_PlayerMessage.Open          = RegisterWindowMessageA( "PodHacks_Open" );
    g_PlayerMessage.SetTimeFormat = RegisterWindowMessageA( "PodHacks_SetTimeFormat" );
    g_PlayerMessage.GetItemLength = RegisterWindowMessageA( "PodHacks_GetItemLength" );
    g_PlayerMessage.SetCallback   = RegisterWindowMessageA( "PodHacks_SetCallback" );
    g_PlayerMessage.Play          = RegisterWindowMessageA( "PodHacks_Play" );
    g_PlayerMessage.Stop          = RegisterWindowMessageA( "PodHacks_Stop" );
    g_PlayerMessage.Close         = RegisterWindowMessageA( "PodHacks_Close" );
    g_PlayerMessage.Pause         = RegisterWindowMessageA( "PodHacks_Pause" );
    g_PlayerMessage.Resume        = RegisterWindowMessageA( "PodHacks_Resume" );
    g_PlayerMessage.GetVolume     = RegisterWindowMessageA( "PodHacks_GetVolume" );
    g_PlayerMessage.SetVolume     = RegisterWindowMessageA( "PodHacks_SetVolume" );
    EnumWindows( Player_Find, 0 );
    if( 0 == g_PlayerWindow )
    {
        CHAR ApplicationName[ MAX_PATH ];
        STARTUPINFOA StartupInfo = { sizeof(STARTUPINFOA), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, STARTF_USESHOWWINDOW, SW_SHOWNA, 0, 0, 0, 0, 0 };
        PROCESS_INFORMATION ProcessInformation = { 0, 0, 0, 0 };
        FullAppFileName( ApplicationName, "PodHacks.exe" );
        if( FALSE == CreateProcessA( ApplicationName, "--player", 0, 0, FALSE, 0, 0, 0, &StartupInfo, &ProcessInformation ) )
        {
            CloseHandle( g_PlayerLock );
            g_PlayerLock = 0;
            return MCIERR_DEVICE_OPEN;
        }
        CloseHandle( ProcessInformation.hThread );
        WaitForInputIdle( ProcessInformation.hProcess, INFINITE );
        {
            DWORD Start;
            for( Start = GetTickCount(); 0 == g_PlayerWindow; EnumWindows( Player_Find, 0 ) )
            {
                DWORD Ticks = GetTickCount();
                if( Start > Ticks )
                    Start = Ticks;
                if( Ticks - Start >= 10 * 1000 )
                {
                    CloseHandle( g_PlayerLock );
                    g_PlayerLock = 0;
                    TerminateProcess( ProcessInformation.hProcess, 0 );
                    CloseHandle( ProcessInformation.hProcess );
                    return MCIERR_NO_WINDOW;
                }
            }
        }
        CloseHandle( ProcessInformation.hProcess );
        {
            HWND Wnd;
            for( Wnd = GetNextWindow( g_PlayerWindow, GW_HWNDNEXT ); Wnd; Wnd = GetNextWindow( Wnd, GW_HWNDNEXT ) )
            {
                DWORD ProcessId = 0;
                if( GetWindowThreadProcessId( Wnd, &ProcessId ) &&
                    (GetCurrentProcessId() == ProcessId) &&
                    IsWindowVisible( Wnd ) &&
                    BringWindowToTop( Wnd ) )
                {
                    break;
                }
            }
        }
    }
    return MMSYSERR_NOERROR;
}

MCIERROR Player_GetVolume( DWORD * Volume )
{
    MCIERROR Status = Player_Init();
    if( Status != MMSYSERR_NOERROR )
        return Status;
    *Volume = (DWORD)SendMessageA( g_PlayerWindow, g_PlayerMessage.GetVolume, 0, 0 );
    return MMSYSERR_NOERROR;
}

MCIERROR Player_SetVolume( DWORD Volume )
{
    MCIERROR Status = Player_Init();
    if( Status != MMSYSERR_NOERROR )
        return Status;
    return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.SetVolume, Volume, 0 );
}

UINT g_WavDeviceID;

MCIERROR WINAPI WINMM_mciSendCommandA( MCIDEVICEID mciId, UINT uMsg, DWORD_PTR dwParam1, DWORD_PTR dwParam2 )
{
    static MCIDEVICEID s_DeviceID;

    if( (MCI_DEVTYPE_CD_AUDIO == mciId) && (MCI_SYSINFO == uMsg) && (MCI_SYSINFO_QUANTITY == dwParam1) )
    {
        MCIERROR Status = mciSendCommandA( mciId, uMsg, dwParam1, dwParam2 );
        if( MMSYSERR_NOERROR == Status )
        {
            LPMCI_SYSINFO_PARMSA lpSysInfo = (LPMCI_SYSINFO_PARMSA)dwParam2;
            if( 4 == lpSysInfo->dwRetSize )
            {
                LPDWORD lpReturn = (LPDWORD)(lpSysInfo->lpstrReturn);
                if( lpReturn && (0 == *lpReturn) )
                    *lpReturn = 1;
            }
        }
        return Status;
    }
    if( (0 == mciId) && (MCI_OPEN == uMsg) && (MCI_OPEN_TYPE_ID & dwParam1) )
    {
        LPMCI_OPEN_PARMSA lpOpen = (LPMCI_OPEN_PARMSA)dwParam2;
        if( MCI_DEVTYPE_CD_AUDIO == (WORD)(DWORD_PTR)(lpOpen->lpstrDeviceType) )
        {
            if( 0 == g_WavDeviceID )
            {
                MCIERROR Status = Player_Init();
                if( MMSYSERR_NOERROR == Status )
                {
                    Status = (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.Open, 0, 0 );
                    if( MMSYSERR_NOERROR == Status )
                    {
                        s_DeviceID = MCI_DEVTYPE_FIRST_USER;
                        lpOpen->wDeviceID = s_DeviceID;
                    }
                }
                return Status;
            }
            {
                CHAR FullPath[ MAX_PATH ];
                MCI_WAVE_OPEN_PARMSA Open;
                FullAppFileName( FullPath, "Track02.wav" );
                Open.dwCallback = lpOpen->dwCallback;
                Open.wDeviceID = 0;
                Open.lpstrDeviceType = (LPCSTR)MCI_DEVTYPE_WAVEFORM_AUDIO;
                Open.lpstrElementName = FullPath;
                Open.lpstrAlias = NULL;
                Open.dwBufferSeconds = 0;
                if( MMSYSERR_NOERROR == mciSendCommandA( 0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID | MCI_OPEN_ELEMENT | MCI_WAIT, (DWORD_PTR)&Open ) )
                {
                    s_DeviceID = Open.wDeviceID;
                    lpOpen->wDeviceID = s_DeviceID;
                    if( g_WavDeviceID > 1 )
                    {
                        MCI_WAVE_SET_PARMS Set = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };
                        Set.wOutput = g_WavDeviceID - 1;
                        mciSendCommandA( s_DeviceID, MCI_SET, MCI_WAVE_OUTPUT, (DWORD_PTR)&Set );
                    }
                    return MMSYSERR_NOERROR;
                }
                FullAppFileName( FullPath, "Track02.mp3" );
                Open.lpstrDeviceType = "MPEGVideo";
                if( MMSYSERR_NOERROR == mciSendCommandA( 0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_WAIT, (DWORD_PTR)&Open ) )
                {
                    s_DeviceID = Open.wDeviceID;
                    lpOpen->wDeviceID = s_DeviceID;
                    return MMSYSERR_NOERROR;
                }
            }
        }
    }
    if( s_DeviceID && (s_DeviceID == mciId) )
    {
        if( 0 == g_WavDeviceID )
        {
            if( 0 == g_PlayerWindow )
                return MCIERR_DEVICE_NOT_READY;
            if( (MCI_SET == uMsg) && (MCI_SET_TIME_FORMAT == dwParam1) )
            {
                LPMCI_SET_PARMS lpSet = (LPMCI_SET_PARMS)dwParam2;
                return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.SetTimeFormat, (WPARAM)(lpSet->dwTimeFormat), 0 );
            }
            else if( (MCI_STATUS == uMsg) && ((MCI_STATUS_ITEM | MCI_TRACK) == dwParam1) )
            {
                LPMCI_STATUS_PARMS lpStatus = (LPMCI_STATUS_PARMS)dwParam2;
                if( (MCI_STATUS_LENGTH == lpStatus->dwItem) )
                {
                    lpStatus->dwReturn = (DWORD_PTR)SendMessageA( g_PlayerWindow, g_PlayerMessage.GetItemLength, (WPARAM)(lpStatus->dwTrack), 0 );
                    if( 0 == lpStatus->dwReturn )
                        return MCIERR_FILE_NOT_FOUND;
                    return MMSYSERR_NOERROR;
                }
            }
            else if( (MCI_PLAY == uMsg) && (dwParam1 & MCI_TO) )
            {
                LPMCI_PLAY_PARMS lpPlay = (LPMCI_PLAY_PARMS)dwParam2;
                {
                    DWORD_PTR Callback = (dwParam1 & MCI_NOTIFY) ? lpPlay->dwCallback : 0;
                    SendMessageA( g_PlayerWindow, g_PlayerMessage.SetCallback, (WPARAM)Callback, 0 );
                }
                if( dwParam1 & MCI_FROM )
                    return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.Play, (WPARAM)(lpPlay->dwFrom), (LPARAM)(lpPlay->dwTo) );
                return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.Resume, (WPARAM)(lpPlay->dwTo), 0 );
            }
            else if( MCI_STOP == uMsg )
            {
                {
                    DWORD_PTR Callback = (dwParam1 & MCI_NOTIFY) ? ((LPMCI_GENERIC_PARMS)dwParam2)->dwCallback : 0;
                    SendMessageA( g_PlayerWindow, g_PlayerMessage.SetCallback, (WPARAM)Callback, 0 );
                }
                return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.Stop, 0, 0 );
            }
            else if( MCI_PAUSE == uMsg )
            {
                return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.Pause, 0, 0 );
            }
            else if( MCI_CLOSE == uMsg )
            {
                return (MCIERROR)SendMessageA( g_PlayerWindow, g_PlayerMessage.Close, 0, 0 );
            }
            return MCIERR_UNRECOGNIZED_COMMAND;
        }

        if( (MCI_SET == uMsg) && (MCI_SET_TIME_FORMAT == dwParam1) )
        {
            LPMCI_SET_PARMS lpSet = (LPMCI_SET_PARMS)dwParam2;
            lpSet->dwTimeFormat = MCI_FORMAT_MILLISECONDS;
            return mciSendCommandA( mciId, uMsg, dwParam1, dwParam2 );
        }
        else if( (MCI_STATUS == uMsg) && ((MCI_STATUS_ITEM | MCI_TRACK) == dwParam1) )
        {
            LPMCI_STATUS_PARMS lpStatus = (LPMCI_STATUS_PARMS)dwParam2;
            if( (MCI_STATUS_LENGTH == lpStatus->dwItem) )
            {
                if( MMSYSERR_NOERROR == mciSendCommandA( mciId, MCI_STATUS, MCI_STATUS_ITEM, dwParam2 ) )
                {
                    lpStatus->dwReturn = MCI_MAKE_MSF(
                        lpStatus->dwReturn / 60000,
                        (lpStatus->dwReturn % 60000) / 1000,
                        (lpStatus->dwReturn % 1000) * 75 / 1000 );
                    return MMSYSERR_NOERROR;
                }
            }
        }
        else if( (MCI_PLAY == uMsg) && (dwParam1 & MCI_TO) )
        {
            LPMCI_PLAY_PARMS lpPlay = (LPMCI_PLAY_PARMS)dwParam2;
            if( dwParam1 & MCI_FROM )
            {
                if( MCI_TMSF_TRACK( lpPlay->dwTo ) > MCI_TMSF_TRACK( lpPlay->dwFrom ) )
                {
                    MCI_STATUS_PARMS Status;
                    Status.dwCallback = lpPlay->dwCallback;
                    Status.dwReturn = 0;
                    Status.dwItem = MCI_STATUS_LENGTH;
                    Status.dwTrack = 0;
                    if( MMSYSERR_NOERROR == mciSendCommandA( mciId, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&Status ) )
                        lpPlay->dwTo = Status.dwReturn;
                }
                else
                {
                    lpPlay->dwTo =
                        (MCI_TMSF_FRAME( lpPlay->dwTo ) * 1000 / 75) +
                        (MCI_TMSF_SECOND( lpPlay->dwTo ) * 1000) +
                        (MCI_TMSF_MINUTE( lpPlay->dwTo ) * 60000);
                }
                lpPlay->dwFrom =
                    (MCI_TMSF_FRAME( lpPlay->dwFrom ) * 1000 / 75) +
                    (MCI_TMSF_SECOND( lpPlay->dwFrom ) * 1000) +
                    (MCI_TMSF_MINUTE( lpPlay->dwFrom ) * 60000);
            }
            else
            {
                lpPlay->dwTo =
                    (MCI_TMSF_FRAME( lpPlay->dwTo ) * 1000 / 75) +
                    (MCI_TMSF_SECOND( lpPlay->dwTo ) * 1000) +
                    (MCI_TMSF_MINUTE( lpPlay->dwTo ) * 60000);
            }
            return mciSendCommandA( mciId, uMsg, dwParam1, dwParam2 );
        }
    }
    return mciSendCommandA( mciId, uMsg, dwParam1, dwParam2 );
}

DWORD g_Mixer_CompactDisc_Line;

MMRESULT WINAPI WINMM_mixerGetLineInfoA( HMIXEROBJ hmxobj, LPMIXERLINEA pmxl, DWORD fdwInfo )
{
    MMRESULT Result = mixerGetLineInfoA( hmxobj, pmxl, fdwInfo );
    if( (MMSYSERR_NOERROR == Result) &&
        ((MIXER_GETLINEINFOF_COMPONENTTYPE | MIXER_OBJECTF_HMIXER) == fdwInfo) &&
        (MIXERLINE_COMPONENTTYPE_SRC_COMPACTDISC == pmxl->dwComponentType) )
    {
        g_Mixer_CompactDisc_Line = pmxl->dwLineID;
    }
    return Result;
}

DWORD g_Mixer_CompactDisc_Control;

MMRESULT WINAPI WINMM_mixerGetLineControlsA( HMIXEROBJ hmxobj, LPMIXERLINECONTROLSA pmxlc, DWORD fdwControls )
{
    MMRESULT Result = mixerGetLineControlsA( hmxobj, pmxlc, fdwControls );
    if( (MMSYSERR_NOERROR == Result) &&
        ((MIXER_GETLINECONTROLSF_ONEBYTYPE | MIXER_OBJECTF_HMIXER) == fdwControls) &&
        (MIXERCONTROL_CONTROLTYPE_VOLUME == pmxlc->dwControlType) &&
        (g_Mixer_CompactDisc_Line == pmxlc->dwLineID) )
    {
        g_Mixer_CompactDisc_Control = pmxlc->pamxctrl->dwControlID;
    }
    return Result;
}

MMRESULT WINAPI WINMM_mixerGetControlDetailsA( HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails )
{
    if( ((MIXER_GETCONTROLDETAILSF_VALUE | MIXER_OBJECTF_HMIXER) == fdwDetails) &&
        pmxcd && (pmxcd->dwControlID == g_Mixer_CompactDisc_Control) &&
        pmxcd->paDetails )
    {
        if( 0 == g_WavDeviceID )
        {
            DWORD Volume;
            MCIERROR Status = Player_GetVolume( &Volume );
            if( MMSYSERR_NOERROR == Status )
                ((LPMIXERCONTROLDETAILS_UNSIGNED)(pmxcd->paDetails))->dwValue = LOWORD(Volume);
            return Status;
        }
        if( g_WavDeviceID > 1 )
        {
            DWORD Volume = 0;
            if( MMSYSERR_NOERROR == waveOutGetVolume( (HWAVEOUT)(g_WavDeviceID - 1), &Volume ) )
            {
                ((LPMIXERCONTROLDETAILS_UNSIGNED)(pmxcd->paDetails))->dwValue = LOWORD(Volume);
                return MMSYSERR_NOERROR;
            }
        }
    }
    return mixerGetControlDetailsA( hmxobj, pmxcd, fdwDetails );
}

MMRESULT WINAPI WINMM_mixerSetControlDetails( HMIXEROBJ hmxobj, LPMIXERCONTROLDETAILS pmxcd, DWORD fdwDetails )
{
    if( ((MIXER_SETCONTROLDETAILSF_VALUE | MIXER_OBJECTF_HMIXER) == fdwDetails) &&
        pmxcd && (pmxcd->dwControlID == g_Mixer_CompactDisc_Control) &&
        pmxcd->paDetails )
    {
        if( 0 == g_WavDeviceID )
        {
            DWORD Value = LOWORD(((LPMIXERCONTROLDETAILS_UNSIGNED)(pmxcd->paDetails))->dwValue);
            DWORD Volume = MAKELONG( Value, Value );
            return Player_SetVolume( Volume );
        }
        else if( g_WavDeviceID > 1 )
        {
            DWORD Value = LOWORD(((LPMIXERCONTROLDETAILS_UNSIGNED)(pmxcd->paDetails))->dwValue);
            DWORD Volume = MAKELONG( Value, Value );
            if( MMSYSERR_NOERROR == waveOutSetVolume( (HWAVEOUT)(g_WavDeviceID - 1), Volume ) )
                return MMSYSERR_NOERROR;
        }
    }
    return mixerSetControlDetails( hmxobj, pmxcd, fdwDetails );
}

/************************************************************************/
/* Suppress resolution changes for better Glide wrapper support         */
/************************************************************************/

BOOL g_NoResSwitch;
BOOL g_DisplayModeHighRes;

UINT WINAPI KERNEL32_GetPrivateProfileIntA( LPCSTR lpAppName, LPCSTR lpKeyName, INT nDefault, LPCSTR lpFileName )
{
    static BOOL s_DisplayModeRead;
    if( (0 == s_DisplayModeRead) && lpAppName && lpKeyName &&
        ('p' == (0x20 | lpAppName[ 0 ])) &&
        ('o' == (0x20 | lpAppName[ 1 ])) &&
        ('d' == (0x20 | lpAppName[ 2 ])) &&
        (0 == lstrcmpiA( lpKeyName, "Configuration" )) )
    {
        s_DisplayModeRead = TRUE;
        switch( GetPrivateProfileIntA( lpAppName, "DisplayMode", 0, lpFileName ) )
        {
        case 6:
        case 7:
            g_DisplayModeHighRes = TRUE;
        }
    }
    return GetPrivateProfileIntA( lpAppName, lpKeyName, nDefault, lpFileName );
}

typedef int (__stdcall * PGRSSTWINOPEN)( unsigned long, signed long, signed long, signed long, signed long, int, int );
PGRSSTWINOPEN grSstWinOpen;

int s_SstWinOpenResult;

int __stdcall GLIDE2X_grSstWinOpen( unsigned long hWnd, signed long screen_resolution, signed long refresh_rate, signed long color_format, signed long origin_location, int nColBuffers, int nAuxBuffers )
{
    if( 0 == s_SstWinOpenResult )
    {
        if( g_DisplayModeHighRes )
            screen_resolution = 8;  /* GR_RESOLUTION_800x600 */
        s_SstWinOpenResult = grSstWinOpen( hWnd, screen_resolution, refresh_rate, color_format, origin_location, nColBuffers, nAuxBuffers );
    }
    return s_SstWinOpenResult;
}

typedef void (__stdcall * PGRSSTWINCLOSE)( void );
PGRSSTWINCLOSE grSstWinClose;

BOOL g_SstWinClosePending;

void __stdcall GLIDE2X_grSstWinClose( void )
{
    if( s_SstWinOpenResult )
        g_SstWinClosePending = TRUE;
    else
        grSstWinClose();
}

typedef void (__stdcall * PGRGLIDESHUTDOWN)( void );
PGRGLIDESHUTDOWN grGlideShutdown;

void __stdcall GLIDE2X_grGlideShutdown( void )
{
    if( g_SstWinClosePending )
        grSstWinClose();
    grGlideShutdown();
}

/************************************************************************/
/* Create main window before grSstOpen (POD Demos with Glide wrappers)  */
/************************************************************************/

WNDPROC       g_PodWndProc;
LONG volatile g_PodWndCreated;
LONG volatile g_PodWndActivated;

LRESULT CALLBACK PodWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam )
{
    if( g_PodWndProc && g_PodWndCreated )
    {
        if( 0 == g_PodWndActivated )
        {
            InterlockedIncrement( &g_PodWndActivated );
            g_PodWndProc( hWnd, WM_ACTIVATE, WA_ACTIVE, 0 );
            g_PodWndProc( hWnd, WM_SETFOCUS, 0, 0 );
        }
        return CallWindowProcA( g_PodWndProc, hWnd, Msg, wParam, lParam );
    }
    return DefWindowProcA( hWnd, Msg, wParam, lParam );
}

HWND g_PodWnd;

HWND WINAPI USER32_CreateWindowExA( DWORD dwExStyle, LPCSTR lpClassName, LPCSTR lpWindowName, DWORD dwStyle, int X, int Y, int nWidth, int nHeight, HWND hWndParent, HMENU hMenu, HINSTANCE hInstance, LPVOID lpParam )
{
    if( g_PodWnd && lpClassName && (0 == lstrcmpiA( lpClassName, "Fenetre Pod" )) )
    {
        if( g_PodWndProc )
        {
            CREATESTRUCTA lParam;
            lParam.lpCreateParams = lpParam;
            lParam.hInstance      = hInstance;
            lParam.hMenu          = hMenu;
            lParam.hwndParent     = hWndParent;
            lParam.cy             = nWidth;
            lParam.cx             = nHeight;
            lParam.y              = Y;
            lParam.x              = X;
            lParam.style          = dwStyle;
            lParam.lpszName       = lpWindowName;
            lParam.lpszClass      = lpClassName;
            lParam.dwExStyle      = dwExStyle;
            g_PodWndProc( g_PodWnd, WM_CREATE, 0, (LPARAM)&lParam );
            InterlockedIncrement( &g_PodWndCreated );
        }
        return g_PodWnd;
    }
    g_PodWnd = CreateWindowExA( dwExStyle, lpClassName, lpWindowName, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu, hInstance, lpParam );
    return g_PodWnd;
}

ATOM g_PodWndClass;

ATOM WINAPI USER32_RegisterClassA( CONST WNDCLASSA * lpWndClass )
{
    if( g_PodWndClass &&
        lpWndClass && lpWndClass->lpszClassName &&
        (0 == lstrcmpiA( lpWndClass->lpszClassName, "Fenetre Pod" )) )
    {
        g_PodWndProc = lpWndClass->lpfnWndProc;
        return g_PodWndClass;
    }
    return RegisterClassA( lpWndClass );
}

typedef int (__stdcall * PGRSSTOPEN)( signed long, signed long, signed long, signed long, signed long, int );
PGRSSTOPEN grSstOpen;

int s_SstOpenResult;

int __stdcall GLIDE_grSstOpen( signed long screen_resolution, signed long refresh_rate, signed long color_format, signed long origin_location, signed long smoothing_filter, int num_buffers )
{
    if( NULL == g_PodWnd )
    {
        WNDCLASSA WndClass;
        WndClass.style         = CS_VREDRAW | CS_HREDRAW | CS_DBLCLKS;
        WndClass.lpfnWndProc   = PodWndProc;
        WndClass.cbClsExtra    = 0;
        WndClass.cbWndExtra    = 0;
        WndClass.hInstance     = g_AppModule;
        WndClass.hIcon         = LoadIconA( g_AppModule, "ICO_APOD_1" );
        WndClass.hCursor       = LoadCursorA( NULL, IDC_ARROW );
        WndClass.hbrBackground = GetStockObject( BLACK_BRUSH );
        WndClass.lpszMenuName  = NULL;
        WndClass.lpszClassName = "Fenetre Pod";
        g_PodWndClass = RegisterClassA( &WndClass );
        g_PodWnd = CreateWindowExA( 0, "Fenetre Pod", "Pod", WS_POPUPWINDOW | WS_DLGFRAME | WS_MAXIMIZE | WS_VISIBLE, 0, 0, 640, 480, NULL, NULL, g_AppModule, NULL );
    }
    if( g_NoResSwitch )
    {
        if( 0 == s_SstOpenResult)
            s_SstOpenResult = grSstOpen( screen_resolution, refresh_rate, color_format, origin_location, smoothing_filter, num_buffers );
        return s_SstOpenResult;
    }
    return grSstOpen( screen_resolution, refresh_rate, color_format, origin_location, smoothing_filter, num_buffers );
}

/************************************************************************/
/* Enable Reverse Mode by default                                       */
/*                                                                      */
/*  UbiSoft\Ubi.ini                                                     */
/*  [POD2_0]                                                            */
/*  Tonneau="%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld "                  */
/*                                                                      */
/*  struct pod2_tonneau_t {                                             */
/*    long csum;     // (key1 + key2 + data[*]) % 213                   */
/*    long key1;     // data[0] ^= key1                                 */
/*    long key2;     // data[data[0]] ^= key2                           */
/*    long data[7];  // flags = data[data[0]]                           */
/*    #define POD2_TONNEAU_REVERSE_EASY   0x08                          */
/*    #define POD2_TONNEAU_REVERSE_NORMAL 0x40                          */
/*    #define POD2_TONNEAU_REVERSE_HARD   0x02                          */
/*  }                                                                   */
/*  The game uses sscanf to write ten 32-bit signed decimal integers    */
/*  into an internal structure (I named it pod2_tonneau_t). The return  */
/*  value (count of scanned values) is validated - therefore we cannot  */
/*  make the value shorter than 10 integers. The whole structure is     */
/*  check-summed by adding all integers and calculating the (signed)    */
/*  remainder of the division by 213. The first data element (data[0])  */
/*  is xor-decoded with key1 and is used as index into the data array.  */
/*  The data[index] element is xor-decoded with key2 and is tested for  */
/*  the Reverse mode flags.                                             */
/*                                                                      */
/************************************************************************/

DWORD WINAPI KERNEL32_GetPrivateProfileStringA( LPCSTR lpAppName, LPCSTR lpKeyName, LPCSTR lpDefault, LPSTR lpReturnedString, DWORD nSize, LPCSTR lpFileName )
{
    if( lpAppName && lpKeyName &&
        ('p' == (0x20 | lpAppName[ 0 ])) &&
        ('o' == (0x20 | lpAppName[ 1 ])) &&
        ('d' == (0x20 | lpAppName[ 2 ])) &&
        (0 == lstrcmpiA( lpKeyName, "Tonneau" )) )
    {
        /* Easy to remember but technically tricky:  */
        /* - initial state after reading             */
        /*   { -2, -2, 0, {  0, 0, 0, 0, 0, 0, 0 } } */
        /* - the index is decoded:                   */
        /*   data[0] = data[0] ^ key1                */
        /*   data[0] = 0 ^ -2                        */
        /*   data[0] = -2                            */
        /*   { -2, -2, 0, { -2, 0, 0, 0, 0, 0, 0 } } */
        /* - the flags are decoded:                  */
        /*   data[data[0]] = data[data[0]] ^ key2    */
        /*   data[-2] = data[-2] ^ key2              */
        /*   key1 = key1 ^ key2 (data[-2] = key1)    */
        /*   key1 = key1 ^ 0                         */
        /*   { -2, -2, 0, { -2, 0, 0, 0, 0, 0, 0 } } */
        /* - the flags are tested:                   */
        /*   flags = data[data[0]]                   */
        /*   flags = data[-2]                        */
        /*   flags = key1 (data[-2] = key1)          */
        /*   flags = -2                              */
        /*   (the three flags are all set)           */
        static CHAR Default[] = "-2 -2 0 0 0 0 0 0 0 0";
        lpDefault = Default;
    }
    return GetPrivateProfileStringA( lpAppName, lpKeyName, lpDefault, lpReturnedString, nSize, lpFileName );
}

/************************************************************************/
/* Limit DirectDraw to approximately 60 flips/second                    */
/************************************************************************/

STDMETHOD(g_DDS_Flip)( LPDIRECTDRAWSURFACE, LPDIRECTDRAWSURFACE, DWORD );
STDMETHODIMP DDRAW_IDirectDrawSurface_Flip( LPDIRECTDRAWSURFACE This, LPDIRECTDRAWSURFACE lpDDSurfaceTargetOverride, DWORD dwFlags )
{
    HRESULT Result = g_DDS_Flip( This, lpDDSurfaceTargetOverride, dwFlags );
    if( DD_OK == Result )
    {
        static DWORD s_FlipTime = 0;
        DWORD FlipTime;
        /* 1000 / (1000 / 60) = 62.5 flips/second */
        for( FlipTime = timeGetTime(); 1000 / 60 > FlipTime - s_FlipTime; FlipTime = timeGetTime() )
        {
            MSG Msg;
            if( PeekMessageA( &Msg, NULL, 0, 0, PM_REMOVE | PM_QS_INPUT ) )
            {
                TranslateMessage( &Msg );
                DispatchMessageA( &Msg );
            }
        }
        s_FlipTime = FlipTime;
    }
    return Result;
}

LPDIRECTDRAW g_DD;

STDMETHOD(g_DD_CreateSurface)( LPDIRECTDRAW, LPDDSURFACEDESC, LPDIRECTDRAWSURFACE *, IUnknown * );
STDMETHODIMP DDRAW_IDirectDraw_CreateSurface( LPDIRECTDRAW This, LPDDSURFACEDESC lpDDSurfaceDesc, LPDIRECTDRAWSURFACE * lplpDDSurface, IUnknown * pUnkOuter )
{
    HRESULT Result = g_DD_CreateSurface( This, lpDDSurfaceDesc, lplpDDSurface, pUnkOuter );
    if( (DD_OK == Result) && (This == g_DD) &&
        lpDDSurfaceDesc && (DDSD_CAPS & lpDDSurfaceDesc->dwFlags) && (DDSCAPS_PRIMARYSURFACE & lpDDSurfaceDesc->ddsCaps.dwCaps) &&
        lplpDDSurface && *lplpDDSurface )
    {
        LPDIRECTDRAWSURFACE Primary = *lplpDDSurface;
        if( DDRAW_IDirectDrawSurface_Flip != Primary->lpVtbl->Flip )
        {
            g_DDS_Flip = Primary->lpVtbl->Flip;
            {
                DWORD OldProtect = 0;
                if( VirtualProtect( Primary->lpVtbl, sizeof(IDirectDrawSurfaceVtbl), PAGE_EXECUTE_READWRITE, &OldProtect ) )
                {
                    Primary->lpVtbl->Flip = DDRAW_IDirectDrawSurface_Flip;
                    VirtualProtect( Primary->lpVtbl, sizeof(IDirectDrawSurfaceVtbl), OldProtect, &OldProtect );
                }
            }
        }
    }
    return Result;
}

/************************************************************************/
/* Fake DirectDraw display modes (and avoid buffer overflow in POD D3D) */
/************************************************************************/

STDMETHODIMP DDRAW_IDirectDraw_EnumDisplayModes( LPDIRECTDRAW This, DWORD dwFlags, LPDDSURFACEDESC lpDDSurfaceDesc, LPVOID lpContext, LPDDENUMMODESCALLBACK lpEnumModesCallback )
{
    UNREFERENCED_PARAMETER( This );
    UNREFERENCED_PARAMETER( dwFlags );
    UNREFERENCED_PARAMETER( lpDDSurfaceDesc );

    if( NULL == lpEnumModesCallback )
        return DDERR_INVALIDPARAMS;
    {
        DDSURFACEDESC DDSurfaceDesc;
        DDSurfaceDesc.dwSize = sizeof(DDSURFACEDESC);
        DDSurfaceDesc.dwFlags = DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
        DDSurfaceDesc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        DDSurfaceDesc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        DDSurfaceDesc.ddpfPixelFormat.dwRGBBitCount = 16;
        DDSurfaceDesc.ddpfPixelFormat.dwRBitMask = 0x0000F800;
        DDSurfaceDesc.ddpfPixelFormat.dwGBitMask = 0x000007E0;
        DDSurfaceDesc.ddpfPixelFormat.dwBBitMask = 0x0000001F;
        DDSurfaceDesc.dwWidth = 640;
        DDSurfaceDesc.dwHeight = 480;
        if( DDENUMRET_CANCEL == lpEnumModesCallback( &DDSurfaceDesc, lpContext ) )
            return DD_OK;
        DDSurfaceDesc.dwHeight = 400;
        if( DDENUMRET_CANCEL == lpEnumModesCallback( &DDSurfaceDesc, lpContext ) )
            return DD_OK;
        DDSurfaceDesc.dwWidth = 512;
        DDSurfaceDesc.dwHeight = 384;
        if( DDENUMRET_CANCEL == lpEnumModesCallback( &DDSurfaceDesc, lpContext ) )
            return DD_OK;
        DDSurfaceDesc.dwWidth = 800;
        DDSurfaceDesc.dwHeight = 600;
        if( DDENUMRET_CANCEL == lpEnumModesCallback( &DDSurfaceDesc, lpContext ) )
            return DD_OK;
    }
    return DD_OK;
}

LONG g_DDEnum;
BOOL g_DDFlipLimit;
BOOL g_DDFakeModes;

HRESULT WINAPI DDRAW_DirectDrawCreate( GUID * lpGUID, LPDIRECTDRAW * lplpDD, IUnknown * pUnkOuter )
{
    HRESULT Result = DirectDrawCreate( lpGUID, lplpDD, pUnkOuter );
    if( !g_DDEnum && (DD_OK == Result) && lplpDD && *lplpDD )
    {
        g_DD = *lplpDD;
        if( g_DDFlipLimit && (DDRAW_IDirectDraw_CreateSurface != g_DD->lpVtbl->CreateSurface) )
        {
            g_DD_CreateSurface = g_DD->lpVtbl->CreateSurface;
            {
                DWORD OldProtect = 0;
                if( VirtualProtect( g_DD->lpVtbl, sizeof(IDirectDrawVtbl), PAGE_EXECUTE_READWRITE, &OldProtect ) )
                {
                    g_DD->lpVtbl->CreateSurface = DDRAW_IDirectDraw_CreateSurface;
                    VirtualProtect( g_DD->lpVtbl, sizeof(IDirectDrawVtbl), OldProtect, &OldProtect );
                }
            }
        }
        if( g_DDFakeModes && (DDRAW_IDirectDraw_EnumDisplayModes != g_DD->lpVtbl->EnumDisplayModes) )
        {
            DWORD OldProtect = 0;
            if( VirtualProtect( g_DD->lpVtbl, sizeof(IDirectDrawVtbl), PAGE_EXECUTE_READWRITE, &OldProtect ) )
            {
                g_DD->lpVtbl->EnumDisplayModes = DDRAW_IDirectDraw_EnumDisplayModes;
                VirtualProtect( g_DD->lpVtbl, sizeof(IDirectDrawVtbl), OldProtect, &OldProtect );
            }
        }
    }
    return Result;
}

HRESULT WINAPI DDRAW_DirectDrawEnumerateA( LPDDENUMCALLBACKA lpCallback, LPVOID lpContext )
{
    HRESULT Result;
    ++g_DDEnum;
    Result = DirectDrawEnumerateA( lpCallback, lpContext );
    --g_DDEnum;
    return Result;
}

/************************************************************************/
/* Do not confine the cursor (support Glide wrappers in windowed mode)  */
/************************************************************************/

BOOL WINAPI USER32_ClipCursor( CONST RECT * lpRect )
{
    UNREFERENCED_PARAMETER( lpRect );
    return TRUE;
}

/************************************************************************/
/* Entry Point                                                          */
/************************************************************************/

void GetConfigString( LPCSTR Section, LPCSTR Name, LPSTR Value, LPCSTR Default )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.ini" );
    GetPrivateProfileStringA( Section, Name, Default, Value, MAX_PATH, FullPath );
}

enum PodHacksOption
{
    PodHacksOption_Auto,
    PodHacksOption_On,
    PodHacksOption_Off
};

BOOL IsOptionEnabled( LPCSTR Section, LPCSTR Name, enum PodHacksOption Default, BOOL(* OnAuto)( void ) )
{
    enum PodHacksOption Option;
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.ini" );
    {
        CHAR String[ MAX_PATH ];
        GetPrivateProfileStringA( Section, Name, NULL, String, MAX_PATH, FullPath );
        if( (0 == lstrcmpiA( String, "Auto" )) || (0 == lstrcmpiA( String, "-1" )) )
            Option = PodHacksOption_Auto;
        else if( (0 == lstrcmpiA( String, "On" )) || (0 == lstrcmpiA( String, "True" )) || (0 == lstrcmpiA( String, "1" )) )
            Option = PodHacksOption_On;
        else if( (0 == lstrcmpiA( String, "Off" )) || (0 == lstrcmpiA( String, "False" )) || (0 == lstrcmpiA( String, "0" )) )
            Option = PodHacksOption_Off;
        else if( (0 == lstrcmpiA( String, "Default" )) || ('\0' == String[ 0 ] ) )
            Option = Default;
        else
        {
            INT Value = (INT)GetPrivateProfileIntA( Section, Name, 0, FullPath );
            if( Value < 0 )
                Option = PodHacksOption_Auto;
            else if( Value > 0 )
                Option = PodHacksOption_On;
            else
                Option = Default;
        }
    }
    switch( Option )
    {
    case PodHacksOption_Auto:
        if( OnAuto )
            return OnAuto();
        /* fall-through */
    case PodHacksOption_On:
        return TRUE;
    case PodHacksOption_Off:
    default:
        return FALSE;
    }
}

PIMAGE_NT_HEADERS32 GetAppHeaders( void )
{
    if( g_AppModule )
    {
        PIMAGE_DOS_HEADER DosHeader = (PIMAGE_DOS_HEADER)g_AppModule;
        if( (IMAGE_DOS_SIGNATURE == DosHeader->e_magic) && (DosHeader->e_lfanew > 0) )
        {
            PIMAGE_NT_HEADERS32 Headers = (PIMAGE_NT_HEADERS32)((ULONG_PTR)g_AppModule + (ULONG)(DosHeader->e_lfanew));
            if( (IMAGE_NT_SIGNATURE == Headers->Signature) &&
                (IMAGE_FILE_MACHINE_I386 == Headers->FileHeader.Machine) )
            {
                return Headers;
            }
        }
    }
    return NULL;
}

PIMAGE_IMPORT_DESCRIPTOR GetAppImports( void )
{
    PIMAGE_NT_HEADERS32 Headers = GetAppHeaders();
    if( Headers &&
        (Headers->FileHeader.SizeOfOptionalHeader >= FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT + 1 ])) &&
        (Headers->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].VirtualAddress != 0) &&
        (Headers->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].Size != 0) )
    {
        return (PIMAGE_IMPORT_DESCRIPTOR)((ULONG_PTR)g_AppModule +
            Headers->OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].VirtualAddress);
    }
    return NULL;
}

BOOL IsAppModulePOD( void )
{
    HRSRC ResInfo = FindResourceA( NULL, "DLGINCLUDE", RT_RCDATA );
    if( ResInfo )
    {
        HGLOBAL ResData = LoadResource( NULL, ResInfo );
        if( ResData )
        {
            LPCSTR DlgInclude = LockResource( ResData );
            if( 0 == lstrcmpiA( DlgInclude, "c:\\POD\\pod.h" ) )
                return TRUE;
        }
    }
    return FALSE;
}

void PatchCpuChecks( void )
{
    WORD Index;
    PIMAGE_NT_HEADERS32 Headers = GetAppHeaders();
    PIMAGE_SECTION_HEADER Section = IMAGE_FIRST_SECTION( Headers );
    for( Index = 0; Index < Headers->FileHeader.NumberOfSections; ++Index )
    {
        if( (IMAGE_SCN_CNT_CODE | IMAGE_SCN_MEM_EXECUTE | IMAGE_SCN_MEM_READ) == Section[ Index ].Characteristics )
        {
            WORD Version = 0x0600;
            LPBYTE Code = (LPBYTE)((ULONG_PTR)g_AppModule + Section[ Index ].VirtualAddress);
            LPBYTE Done = &Code[ Section[ Index ].SizeOfRawData - 0x4B ];
            for( ; Code < Done; ++Code )
            {
                /*
                 *  Cyrix 5/2 test (obsolete, false positive on many CPUs)
                 *
                 *  AUTO:00486508   66 31 C0        xor     ax, ax
                 *  AUTO:0048650B   9E              sahf
                 *  AUTO:0048650C   66 B8 05 00     mov     ax, 5
                 *  AUTO:00486510   66 BB 02 00     mov     bx, 2
                 *  AUTO:00486514   F6 F3           div     bl
                 *  AUTO:00486516   9F              lahf
                 *  AUTO:00486517   80 FC 02        cmp     ah, 2
                 *  AUTO:0048651A   75 07           jnz     short $ + 9
                 *  AUTO:0048651C   B8 01 00 00 00  mov     eax, 1
                 *  AUTO:00486521   EB 05           jmp     short $ + 7
                 *  AUTO:00486523   B8 00 00 00 00  mov     eax, 0
                 */
                if( (0x66 == Code[ 0x00 ]) && (0x31 == Code[ 0x01 ]) && (0xC0 == Code[ 0x02 ]) &&
                    (0x9E == Code[ 0x03 ]) &&
                    (0x66 == Code[ 0x04 ]) && (0xB8 == Code[ 0x05 ]) && (0x05 == Code[ 0x06 ]) && (0x00 == Code[ 0x07 ]) &&
                    (0x66 == Code[ 0x08 ]) && (0xBB == Code[ 0x09 ]) && (0x02 == Code[ 0x0A ]) && (0x00 == Code[ 0x0B ]) &&
                    (0xF6 == Code[ 0x0C ]) && (0xF3 == Code[ 0x0D ]) &&
                    (0x9F == Code[ 0x0E ]) &&
                    (0x80 == Code[ 0x0F ]) && (0xFC == Code[ 0x10 ]) && (0x02 == Code[ 0x11 ]) &&
                    (0x75 == Code[ 0x12 ]) && (0x07 == Code[ 0x13 ]) &&
                    (0xB8 == Code[ 0x14 ]) && (0x01 == Code[ 0x15 ]) && (0x00 == Code[ 0x16 ]) && (0x00 == Code[ 0x17 ]) && (0x00 == Code[ 0x18 ]) &&
                    (0xEB == Code[ 0x19 ]) && (0x05 == Code[ 0x1A ]) &&
                    (0xB8 == Code[ 0x1B ]) && (0x00 == Code[ 0x1C ]) && (0x00 == Code[ 0x1D ]) && (0x00 == Code[ 0x1E ]) && (0x00 == Code[ 0x1F ]) )
                {
                    DWORD Protect;
                    if( VirtualProtect( &Code[ 0x15 ], 1, PAGE_EXECUTE_READWRITE, &Protect ) )
                    {
                        /*
                         *  AUTO:0048651C   B8 00 00 00 00  mov     eax, 0
                         */
                        Code[ 0x15 ] = 0x00;
                        VirtualProtect( &Code[ 0x15 ], 1, Protect, &Protect );
                    }
                    Code += 0x0C;
                }
                /*
                 * CPUID Version (POD doesn't handle greater versions correctly)
                 *
                 *  AUTO:0048638C   B8 01 00 00 00  mov     eax, 1
                 *  AUTO:00486391   0F A2           cpuid
                 *  AUTO:00486393   A3 __ __ __ __  mov     ds:s_cpuidVersion, eax
                 */
                if( (0xB8 == Code[ 0x00 ]) && (0x01 == Code[ 0x01 ]) && (0x00 == Code[ 0x02 ]) && (0x00 == Code[ 0x03 ]) && (0x00 == Code[ 0x04 ]) &&
                    (0x0F == Code[ 0x05 ]) && (0xA2 == Code[ 0x06 ]) &&
                    (0xA3 == Code[ 0x07 ]) )
                {
                    DWORD Protect;
                    if( VirtualProtect( &Code[ 0x01 ], 6, PAGE_EXECUTE_READWRITE, &Protect ) )
                    {
                        /*
                         *  AUTO:0048638C   B8 __ __ 00 00  mov     eax, Version
                         *  AUTO:00486391   90              nop
                         *  AUTO:00486392   90              nop
                         */
                        Code[ 0x01 ] = LOBYTE(Version); Code[ 0x02 ] = HIBYTE(Version);
                        Code[ 0x05 ] = 0x90;
                        Code[ 0x06 ] = 0x90;
                        VirtualProtect( &Code[ 0x01 ], 6, Protect, &Protect );
                    }
                    Code += 0x0C;
                }
                /*
                 * CPUID Vendor (modification required to support the CPUID version lie)
                 *
                 *  AUTO:00486398   B8 00 00 00 00          mov     eax, 0
                 *  AUTO:0048639D   0F A2                   cpuid
                 *  AUTO:0048639F   66 89 1D __ __ __ __    mov     word ptr ds:s_cpuidVendor, bx
                 *  AUTO:004863A6   C1 EB 10                shr     ebx, 16
                 *  AUTO:004863A9   66 89 1D __ __ __ __    mov     word ptr ds:s_cpuidVendor + 2, bx
                 *  AUTO:004863B0   66 89 15 __ __ __ __    mov     word ptr ds:s_cpuidVendor + 4, dx
                 *  AUTO:004863B7   C1 EA 10                shr     edx, 16
                 *  AUTO:004863BA   66 89 15 __ __ __ __    mov     word ptr ds:s_cpuidVendor + 6, dx
                 *  AUTO:004863C1   66 89 0D __ __ __ __    mov     word ptr ds:s_cpuidVendor + 8, cx
                 *  AUTO:004863C8   C1 E9 10                shr     ecx, 16
                 *  AUTO:004863CB   66 89 0D __ __ __ __    mov     word ptr ds:s_cpuidVendor + 10, cx
                 *  AUTO:004863D2   BA __ __ __ __          mov     edx, offset strGenuineIntel
                 *  AUTO:004863D7   B8 __ __ __ __          mov     eax, offset s_cpuidVendor
                 *  AUTO:004863DC   E8 __ __ __ __          call    strcmp
                 *  AUTO:004863E1   85 C0                   test    eax, eax
                 */
                if( (0xB8 == Code[ 0x00 ]) && (0x00 == Code[ 0x01 ]) && (0x00 == Code[ 0x02 ]) && (0x00 == Code[ 0x03 ]) && (0x00 == Code[ 0x04 ]) &&
                    (0x0F == Code[ 0x05 ]) && (0xA2 == Code[ 0x06 ]) &&
                    (0x66 == Code[ 0x07 ]) && (0x89 == Code[ 0x08 ]) &&  (0x1D == Code[ 0x09 ]) &&
                    (0xC1 == Code[ 0x0E ]) && (0xEB == Code[ 0x0F ]) &&  (0x10 == Code[ 0x10 ]) &&
                    (0x66 == Code[ 0x11 ]) && (0x89 == Code[ 0x12 ]) &&  (0x1D == Code[ 0x13 ]) &&
                    (0x66 == Code[ 0x18 ]) && (0x89 == Code[ 0x19 ]) &&  (0x15 == Code[ 0x1A ]) &&
                    (0xC1 == Code[ 0x1F ]) && (0xEA == Code[ 0x20 ]) &&  (0x10 == Code[ 0x21 ]) &&
                    (0x66 == Code[ 0x22 ]) && (0x89 == Code[ 0x23 ]) &&  (0x15 == Code[ 0x24 ]) &&
                    (0x66 == Code[ 0x29 ]) && (0x89 == Code[ 0x2A ]) &&  (0x0D == Code[ 0x2B ]) &&
                    (0xC1 == Code[ 0x30 ]) && (0xE9 == Code[ 0x31 ]) &&  (0x10 == Code[ 0x32 ]) &&
                    (0x66 == Code[ 0x33 ]) && (0x89 == Code[ 0x34 ]) &&  (0x0D == Code[ 0x35 ]) &&
                    (0xBA == Code[ 0x3A ]) &&
                    (0xB8 == Code[ 0x3F ]) &&
                    (0xE8 == Code[ 0x44 ]) &&
                    (0x85 == Code[ 0x49 ]) && (0xC0 == Code[ 0x4A ]) )
                {
                    LPCSTR Vendor = (LPCSTR)(*((ULONG *)&Code[ 0x3B ]));
                    if( 0 == lstrcmpiA( Vendor, "AuthenticAMD" ) )
                    {
                        /* if the CPU is tested for AuthenticAMD first, than the */
                        /* __cpuid( 1 ) code follows some bytes after this block */
                        /* and the supported Family.Models are 5.0, 5.1, and 5.6 */
                        Version = 0x0560;
                    }
                    {
                        DWORD Protect;
                        if( VirtualProtect( &Code[ 0x49 ], 1, PAGE_EXECUTE_READWRITE, &Protect ) )
                        {
                            if( 0 == lstrcmpiA( Vendor, "CyrixInstead" ) )
                            {
                                /*
                                 *  AUTO:004863E1   0C 0C                   or      al, 12
                                 */
                                Code[ 0x49 ] = 0x0C;
                            }
                            else
                            {
                                /*
                                 *  AUTO:004863E1   31 0C                   xor     eax, eax
                                 */
                                Code[ 0x49 ] = 0x31;
                            }
                            VirtualProtect( &Code[ 0x49 ], 1, Protect, &Protect );
                        }
                    }
                    Code += 0x4B - 1;
                }
            }
        }
    }
}

void PatchAppImport( ULONG_PTR API, ULONG_PTR Hook )
{
    PIMAGE_IMPORT_DESCRIPTOR Imports = GetAppImports();
    if( NULL == Imports )
        return;
    while( Imports->Characteristics )
    {
        PIMAGE_THUNK_DATA Thunk = (PIMAGE_THUNK_DATA)((ULONG_PTR)g_AppModule + Imports->OriginalFirstThunk);
        PIMAGE_THUNK_DATA Table = (PIMAGE_THUNK_DATA)((ULONG_PTR)g_AppModule + Imports->FirstThunk);
        while( Thunk->u1.Ordinal )
        {
            if( Table->u1.Function == API )
            {
                DWORD Protect;
                if( VirtualProtect( &(Table->u1.Function), sizeof(IMAGE_THUNK_DATA), PAGE_EXECUTE_READWRITE, &Protect ) )
                {
                    Table->u1.Function = Hook;
                    VirtualProtect( &(Table->u1.Function), sizeof(IMAGE_THUNK_DATA), Protect, &Protect );
                }
            }
            Thunk++;
            Table++;
        }
        Imports++;
    }
}

BOOL NoPodCDInDrive( void )
{
    BOOL result = TRUE;
    UINT Mode = SetErrorMode( SEM_FAILCRITICALERRORS | SEM_NOALIGNMENTFAULTEXCEPT | SEM_NOOPENFILEERRORBOX );
    {
        DWORD Drives;
        CHAR Path[ 4 ] = "A:\\";
        for( Drives = GetLogicalDrives(); Drives; Drives >>= 1, ++(Path[ 0 ]) )
        {
            if( (1 == (1 & Drives)) &&
                (DRIVE_CDROM == GetDriveTypeA( Path )) )
            {
                CHAR Name[ MAX_PATH ];
                DWORD Serial;
                DWORD Length;
                DWORD Flags;
                CHAR Format[ MAX_PATH ];
                if( GetVolumeInformationA( Path, Name, MAX_PATH, &Serial, &Length, &Flags, Format, MAX_PATH ) )
                {
                    /* volume labels supported by POD and POD2_0 */
                    if( (0 == lstrcmpiA( Name, "POD" )) ||
                        (0 == lstrcmpiA( Name, "POD_POD_POD" )) ||
                        (0 == lstrcmpiA( Name, "POD2_0" )) )
                    {
                        /* this is more strict than POD[2_0] */
                        lstrcpynA( Name, Path, MAX_PATH );
                        lstrcatA( Name, "PODMMX.EXE" );
                        if( FileExists( Name ) )
                        {
                            result = FALSE;
                            break;
                        }
                    }
                }
            }
        }
    }
    SetErrorMode( Mode );
    return result;
}

BOOL TrackAvailable( void )
{
    return AppFileExists( "Track02.mp3" ) || AppFileExists( "Track02.wav" );
}

BOOL LocalIniExists( void )
{
    return AppFileExists( "UbiSoft\\Ubi.ini" );
}

BOOL IsNoResSwitchGlideWrapper( void )
{
    LPCSTR FileName;
    if( (FileName = "glide2x.dll", GetModuleHandleA( FileName )) ||
        (FileName = "glide.dll", GetModuleHandleA( FileName )) )
    {
        DWORD Handle;
        DWORD Size = GetFileVersionInfoSizeA( FileName, &Handle );
        if( Size > 0 )
        {
            LPVOID Data = VirtualAlloc( NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
            if( Data )
            {
                if( GetFileVersionInfoA( FileName, Handle, Size, Data ) )
                {
                    struct LANGANDCODEPAGE {
                        WORD wLanguage;
                        WORD wCodePage;
                    } * Translation;
                    UINT Length;
                    if( VerQueryValueA( Data, "\\VarFileInfo\\Translation", &Translation, &Length ) &&
                        (Length >= sizeof(*Translation)) )
                    {
                        CHAR FirstStringFileInfo[ sizeof("\\StringFileInfo\\00000000\\") ];
                        wsprintfA( FirstStringFileInfo, "\\StringFileInfo\\%04x%04x\\", Translation[ 0 ].wLanguage, Translation[ 0 ].wCodePage );
                        /* dgVoodoo 1.x */
                        {
                            CHAR CompanyNameBlock[ sizeof("\\StringFileInfo\\00000000\\CompanyName") ];
                            LPSTR CompanyName;
                            lstrcpyA( CompanyNameBlock, FirstStringFileInfo );
                            lstrcatA( CompanyNameBlock, "CompanyName" );
                            if( VerQueryValueA( Data, CompanyNameBlock, &CompanyName, &Length ) )
                            {
                                if( 0 == lstrcmpiA( CompanyName, "SuckSoftware" ) )
                                {
                                    VirtualFree( Data, 0, MEM_RELEASE );
                                    return TRUE;
                                }
                            }
                        }
                        /* nGlide < 1.02 */
                        {
                            CHAR LegalCopyrightBlock[ sizeof("\\StringFileInfo\\00000000\\LegalCopyright") ];
                            LPSTR LegalCopyright;
                            lstrcpyA( LegalCopyrightBlock, FirstStringFileInfo );
                            lstrcatA( LegalCopyrightBlock, "LegalCopyright" );
                            if( VerQueryValueA( Data, LegalCopyrightBlock, &LegalCopyright, &Length ) )
                            {
                                if( ('n' == LegalCopyright[ 0 ]) &&
                                    ('G' == LegalCopyright[ 1 ]) &&
                                    ('l' == LegalCopyright[ 2 ]) &&
                                    ('i' == LegalCopyright[ 3 ]) &&
                                    ('d' == LegalCopyright[ 4 ]) &&
                                    ('e' == LegalCopyright[ 5 ]) &&
                                    (' ' == LegalCopyright[ 6 ]) &&
                                    ('v' == LegalCopyright[ 7 ]) &&
                                    ((
                                        ('0' == LegalCopyright[ 8 ]) &&
                                        ('.' == LegalCopyright[ 9 ])
                                    ) || (
                                        ('1' == LegalCopyright[ 8 ]) &&
                                        ('.' == LegalCopyright[ 9 ]) &&
                                        ('0' == LegalCopyright[ 10 ]) &&
                                        (('0' == LegalCopyright[ 11 ]) || ('1' == LegalCopyright[ 11 ]))
                                    )) )
                                {
                                    VirtualFree( Data, 0, MEM_RELEASE );
                                    return TRUE;
                                }
                            }
                        }
                    }
                }
                VirtualFree( Data, 0, MEM_RELEASE );
            }
        }
    }
    return FALSE;
}

BOOL IsGogHiResEnabled( void )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "UbiSoft\\Ubi.ini" );
    switch( GetPrivateProfileIntA( "POD2_0", "DisplayMode", 0, FullPath ) )
    {
    case 6:
    case 7:
        return TRUE;
    }
    return FALSE;
}

void PatchGogRes800( void )
{
    HMODULE Module = GetModuleHandleA( "glide2x.dll" );
    if( Module )
    {
        LPBYTE Code = (LPBYTE)(ULONG_PTR)GetProcAddress( Module, "_grSstWinOpen@28" );
        if( Code )
        {
            /*
             * GOG's Glide wrapper ignores the resolution parameter passed to grSstWinOpen
             * (always GR_RESOLUTION_640x480 - but seems to support GR_RESOLUTION_800x600)
             *
             *  .text:10001CB9  C7 05 90 47 02 10 80 02 00 00   mov     g_grScreenWidth, 640
             *  .text:10001CC3  C7 05 94 47 02 10 E0 01 00 00   mov     g_grScreenHeight, 480
             */
            BYTE ScreenX[ 4 ];
            BYTE ScreenY[ 4 ];
            *((ULONG *)ScreenX) = (ULONG_PTR)Module + 0x00024790;
            *((ULONG *)ScreenY) = (ULONG_PTR)Module + 0x00024794;
            if( (0xC7 == Code[ 0x02B9 ]) && (0x05 == Code[ 0x02BA ]) &&
                (ScreenX[ 0 ] == Code[ 0x02BB ]) && (ScreenX[ 1 ] == Code[ 0x02BC ]) && (ScreenX[ 2 ] == Code[ 0x02BD ]) && (ScreenX[ 3 ] == Code[ 0x02BE ]) &&
                (0x80 == Code[ 0x02BF ]) && (0x02 == Code[ 0x02C0 ]) && (0x00 == Code[ 0x02C1 ]) && (0x00 == Code[ 0x02C2 ]) &&
                (0xC7 == Code[ 0x02C3 ]) && (0x05 == Code[ 0x02C4 ]) &&
                (ScreenY[ 0 ] == Code[ 0x02C5 ]) && (ScreenY[ 1 ] == Code[ 0x02C6 ]) && (ScreenY[ 2 ] == Code[ 0x02C7 ]) && (ScreenY[ 3 ] == Code[ 0x02C8 ]) &&
                (0xE0 == Code[ 0x02C9 ]) && (0x01 == Code[ 0x02CA ]) && (0x00 == Code[ 0x02CB ]) && (0x00 == Code[ 0x02CC ]) )
            {
                DWORD Protect;
                if( VirtualProtect( &Code[ 0x02BF ], 0x0C, PAGE_EXECUTE_READWRITE, &Protect ) )
                {
                    /*
                     *  .text:10001CB9  C7 05 90 47 02 10 20 03 00 00   mov     g_grScreenWidth, 800
                     *  .text:10001CC3  C7 05 94 47 02 10 58 02 00 00   mov     g_grScreenHeight, 600
                     */
                    Code[ 0x02BF ] = 0x20; Code[ 0x02C0 ] = 0x03;
                    Code[ 0x02C9 ] = 0x58; Code[ 0x02CA ] = 0x02;
                    VirtualProtect( &Code[ 0x02BF ], 0x0C, Protect, &Protect );
                }
            }
        }
    }
}

void PatchGogMciAPI( void )
{
    HMODULE Module = GetModuleHandleA( "glide2x.dll" );
    if( Module && (0x0000CDC5 == ((PIMAGE_NT_HEADERS32)((ULONG_PTR)Module + (ULONG)(((PIMAGE_DOS_HEADER)Module)->e_lfanew)))->OptionalHeader.AddressOfEntryPoint))
    {
        /*
         *  .text:10007E19  68 30 82 00 10  push    offset WINMM_mciSendCommandA
         *  .text:10007E1E  68 34 45 4E 00  push    0x004E4534
         *  .text:10007E23  E8 78 37 00 00  call    PatchLong
         *  .text:10007E28  83 C4 08        add     esp, 8
         */
        LPBYTE Code = (LPBYTE)Module + 0x00007DA0;
        if( (0x68 == Code[ 0x0079 ]) &&
            (0x68 == Code[ 0x007E ]) && (0x34 == Code[ 0x007F ]) && (0x45 == Code[ 0x0080 ]) && (0x4E == Code[ 0x0081 ]) && (0x00 == Code[ 0x0082 ]) &&
            (0xE8 == Code[ 0x0083 ]) && (0x78 == Code[ 0x0084 ]) && (0x37 == Code[ 0x0085 ]) && (0x00 == Code[ 0x0086 ]) && (0x00 == Code[ 0x0087 ]) &&
            (0x83 == Code[ 0x0088 ]) && (0xC4 == Code[ 0x0089 ]) && (0x08 == Code[ 0x008A ]) )
        {
            DWORD Protect;
            if( VirtualProtect( &Code[ 0x0083 ], 5, PAGE_EXECUTE_READWRITE, &Protect ) )
            {
                /*
                 *  .text:10007E19  68 30 82 00 10  push    offset WINMM_mciSendCommandA
                 *  .text:10007E1E  68 34 45 4E 00  push    0x004E4534
                 *  .text:10007E23  90              nop
                 *  .text:10007E24  90              nop
                 *  .text:10007E25  90              nop
                 *  .text:10007E26  90              nop
                 *  .text:10007E27  90              nop
                 *  .text:10007E28  83 C4 08        add     esp, 8
                 */
                Code[ 0x0083 ] = 0x90;
                Code[ 0x0084 ] = 0x90;
                Code[ 0x0085 ] = 0x90;
                Code[ 0x0086 ] = 0x90;
                Code[ 0x0087 ] = 0x90;
                VirtualProtect( &Code[ 0x0083 ], 5, Protect, &Protect );
            }
        }
    }
}

BOOL APIENTRY DllMain( HMODULE Module, DWORD Reason, LPVOID Reserved )
{
    UNREFERENCED_PARAMETER( Module );
    UNREFERENCED_PARAMETER( Reserved );
    switch( Reason )
    {
    case DLL_PROCESS_ATTACH:
        {
            g_AppModule = GetModuleHandleA( NULL );
            {
                CHAR FileName[ MAX_PATH ];
                GetModuleFileNameA( NULL, FileName, MAX_PATH );
                {
                    LPSTR FilePart;
                    ExtractFileName( FileName, g_AppDirectory, &FilePart );
                    *FilePart = '\0';
                }
            }
            {
                CHAR FullPath[ MAX_PATH ];
                FullAppFileName( FullPath, "PodHacks.log" );
                DeleteFileA( FullPath );
            }
            if( !IsOptionEnabled( "PodHacks", "ValidateExe", PodHacksOption_On, NULL ) || IsAppModulePOD() )
            {
                /*
                 * PodHacks
                 */
                {
                    CHAR FullPath[ MAX_PATH ];
                    FullAppFileName( FullPath, "PodHacks.ini" );
                    g_MiniDumpType = GetPrivateProfileIntA( "PodHacks", "MiniDmpType", -1, FullPath );
                    InitMiniDumpFileName();
                    g_MiniDumpEnabled = (0 == (g_MiniDumpType & ~MiniDumpValidTypeFlags));
                    if( g_MiniDumpEnabled )
                    {
                        PatchAppImport( (ULONG_PTR)SetUnhandledExceptionFilter, (ULONG_PTR)KERNEL32_SetUnhandledExceptionFilter );
                        SetUnhandledExceptionFilter( TopLevelExceptionFilter );
                    }
                    PatchAppImport( (ULONG_PTR)CreateSemaphoreA, (ULONG_PTR)KERNEL32_CreateSemaphoreA );
                }
                /*
                 * API worarounds
                 */
                {
                    PatchAppImport( (ULONG_PTR)CreateBitmap, (ULONG_PTR)GDI32_CreateBitmap );
                    PatchAppImport( (ULONG_PTR)mixerGetDevCapsA, (ULONG_PTR)WINMM_mixerGetDevCapsA );
                    PatchAppImport( (ULONG_PTR)CreateFileA, (ULONG_PTR)KERNEL32_CreateFileA );
                    PatchAppImport( (ULONG_PTR)GetFileTime, (ULONG_PTR)KERNEL32_GetFileTime );
                    PatchAppImport( (ULONG_PTR)GetDiskFreeSpaceA, (ULONG_PTR)KERNEL32_GetDiskFreeSpaceA );
                    {
                        HMODULE GlideModule = GetModuleHandleA("glide.dll");
                        if( GlideModule )
                        {
                            grSstOpen = (PGRSSTOPEN)GetProcAddress( GlideModule, "_grSstOpen@24" );
                            if( grSstOpen )
                            {
                                PatchAppImport( (ULONG_PTR)grSstOpen, (ULONG_PTR)GLIDE_grSstOpen );
                                PatchAppImport( (ULONG_PTR)RegisterClassA, (ULONG_PTR)USER32_RegisterClassA );
                                PatchAppImport( (ULONG_PTR)CreateWindowExA, (ULONG_PTR)USER32_CreateWindowExA );
                            }
                        }
                    }
                }
                /*
                 * POD application
                 */
                if( IsOptionEnabled( "POD", "NoSoundLock", PodHacksOption_On, NULL ) )
                {
                    PatchAppImport( (ULONG_PTR)ReleaseSemaphore, (ULONG_PTR)KERNEL32_ReleaseSemaphore );
                    PatchAppImport( (ULONG_PTR)WaitForSingleObject, (ULONG_PTR)KERNEL32_WaitForSingleObject );
                    PatchAppImport( (ULONG_PTR)waveOutGetDevCapsA, (ULONG_PTR)WINMM_waveOutGetDevCapsA );
                }
                if( IsOptionEnabled( "POD", "PatchCPUIDs", PodHacksOption_On, NULL ) )
                {
                    PatchCpuChecks();
                }
                if( IsOptionEnabled( "POD", "BitsPixel16", PodHacksOption_On, NULL ) )
                {
                    PatchAppImport( (ULONG_PTR)GetDeviceCaps, (ULONG_PTR)GDI32_GetDeviceCaps );
                }
                if( IsOptionEnabled( "POD", "LooseCursor", PodHacksOption_On, NULL ) )
                {
                    PatchAppImport( (ULONG_PTR)ClipCursor, (ULONG_PTR)USER32_ClipCursor );
                }
                if( IsOptionEnabled( "POD", "RedirectCDR", PodHacksOption_Auto, NoPodCDInDrive ) )
                {
                    g_RedirectCDR = TRUE;
                    GetConfigString( "POD", "VolumeLabel", g_CDVolumeName, "POD2_0" );
                    PatchAppImport( (ULONG_PTR)GetDriveTypeA, (ULONG_PTR)KERNEL32_GetDriveTypeA );
                    PatchAppImport( (ULONG_PTR)GetVolumeInformationA, (ULONG_PTR)KERNEL32_GetVolumeInformationA );
                    PatchAppImport( (ULONG_PTR)GetFileSize, (ULONG_PTR)KERNEL32_GetFileSize );
                }
                if( IsOptionEnabled( "POD", "RedirectCDA", PodHacksOption_Auto, TrackAvailable ) )
                {
                    CHAR FullPath[ MAX_PATH ];
                    FullAppFileName( FullPath, "PodHacks.ini" );
                    g_WavDeviceID = GetPrivateProfileIntA( "POD", "WavDeviceID", 0, FullPath );
                    if( g_WavDeviceID != 1 )
                    {
                        PatchAppImport( (ULONG_PTR)mixerGetLineInfoA, (ULONG_PTR)WINMM_mixerGetLineInfoA );
                        PatchAppImport( (ULONG_PTR)mixerGetLineControlsA, (ULONG_PTR)WINMM_mixerGetLineControlsA );
                        PatchAppImport( (ULONG_PTR)mixerGetControlDetailsA, (ULONG_PTR)WINMM_mixerGetControlDetailsA );
                        PatchAppImport( (ULONG_PTR)mixerSetControlDetails, (ULONG_PTR)WINMM_mixerSetControlDetails );
                    }
                    PatchAppImport( (ULONG_PTR)mciSendCommandA, (ULONG_PTR)WINMM_mciSendCommandA );
                    PatchGogMciAPI();
                }
                if( IsOptionEnabled( "POD", "RedirectWIN", PodHacksOption_Auto, LocalIniExists ) )
                {
                    PatchAppImport( (ULONG_PTR)GetWindowsDirectoryA, (ULONG_PTR)KERNEL32_GetWindowsDirectoryA );
                }
                if( IsOptionEnabled( "POD", "NoResSwitch", PodHacksOption_Auto, IsNoResSwitchGlideWrapper ) )
                {
                    HMODULE GlideModule = GetModuleHandleA("glide2x.dll");
                    if( GlideModule )
                    {
                        grSstWinOpen = (PGRSSTWINOPEN)GetProcAddress( GlideModule, "_grSstWinOpen@28" );
                        grSstWinClose = (PGRSSTWINCLOSE)GetProcAddress( GlideModule, "_grSstWinClose@0" );
                        grGlideShutdown = (PGRGLIDESHUTDOWN)GetProcAddress( GlideModule, "_grGlideShutdown@0" );
                        if( grSstWinOpen && grSstWinClose && grGlideShutdown )
                        {
                            PatchAppImport( (ULONG_PTR)GetPrivateProfileIntA, (ULONG_PTR)KERNEL32_GetPrivateProfileIntA );
                            PatchAppImport( (ULONG_PTR)grSstWinOpen, (ULONG_PTR)GLIDE2X_grSstWinOpen );
                            PatchAppImport( (ULONG_PTR)grSstWinClose, (ULONG_PTR)GLIDE2X_grSstWinClose );
                            PatchAppImport( (ULONG_PTR)grGlideShutdown, (ULONG_PTR)GLIDE2X_grGlideShutdown );
                        }
                    }
                    g_NoResSwitch = TRUE;
                }
                if( IsOptionEnabled( "POD", "ReverseMode", PodHacksOption_On, NULL ) )
                {
                    PatchAppImport( (ULONG_PTR)GetPrivateProfileStringA, (ULONG_PTR)KERNEL32_GetPrivateProfileStringA );
                }
                g_DDFlipLimit = IsOptionEnabled( "POD", "DDFlipLimit", PodHacksOption_On, NULL );
                g_DDFakeModes = IsOptionEnabled( "POD", "DDFakeModes", PodHacksOption_Off, NULL );
                if( g_DDFlipLimit || g_DDFakeModes )
                {
                    PatchAppImport( (ULONG_PTR)DirectDrawCreate, (ULONG_PTR)DDRAW_DirectDrawCreate );
                    PatchAppImport( (ULONG_PTR)DirectDrawEnumerateA, (ULONG_PTR)DDRAW_DirectDrawEnumerateA );
                }
                /*
                 * GOG glide2x.dll
                 */
                if( IsOptionEnabled( "GOG", "HighResMode", PodHacksOption_Auto, IsGogHiResEnabled ) )
                {
                    PatchGogRes800();
                }
            }
        }
        break;

    case DLL_PROCESS_DETACH:
        {
            PatchAppImport( (ULONG_PTR)DDRAW_DirectDrawEnumerateA, (ULONG_PTR)DirectDrawEnumerateA );
            PatchAppImport( (ULONG_PTR)DDRAW_DirectDrawCreate, (ULONG_PTR)DirectDrawCreate );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetPrivateProfileStringA, (ULONG_PTR)GetPrivateProfileStringA );
            PatchAppImport( (ULONG_PTR)USER32_CreateWindowExA, (ULONG_PTR)CreateWindowExA );
            PatchAppImport( (ULONG_PTR)USER32_RegisterClassA, (ULONG_PTR)RegisterClassA );
            PatchAppImport( (ULONG_PTR)GLIDE_grSstOpen, (ULONG_PTR)grSstOpen );
            PatchAppImport( (ULONG_PTR)GLIDE2X_grGlideShutdown, (ULONG_PTR)grGlideShutdown );
            PatchAppImport( (ULONG_PTR)GLIDE2X_grSstWinClose, (ULONG_PTR)grSstWinClose );
            PatchAppImport( (ULONG_PTR)GLIDE2X_grSstWinOpen, (ULONG_PTR)grSstWinOpen );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetPrivateProfileIntA, (ULONG_PTR)GetPrivateProfileIntA );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetWindowsDirectoryA, (ULONG_PTR)GetWindowsDirectoryA );
            PatchAppImport( (ULONG_PTR)WINMM_mciSendCommandA, (ULONG_PTR)mciSendCommandA );
            PatchAppImport( (ULONG_PTR)WINMM_mixerSetControlDetails, (ULONG_PTR)mixerSetControlDetails );
            PatchAppImport( (ULONG_PTR)WINMM_mixerGetControlDetailsA, (ULONG_PTR)mixerGetControlDetailsA );
            PatchAppImport( (ULONG_PTR)WINMM_mixerGetLineControlsA, (ULONG_PTR)mixerGetLineControlsA );
            PatchAppImport( (ULONG_PTR)WINMM_mixerGetLineInfoA, (ULONG_PTR)mixerGetLineInfoA );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetDiskFreeSpaceA, (ULONG_PTR)GetDiskFreeSpaceA );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetFileSize, (ULONG_PTR)GetFileSize );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetFileTime, (ULONG_PTR)GetFileTime );
            PatchAppImport( (ULONG_PTR)KERNEL32_CreateFileA, (ULONG_PTR)CreateFileA );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetVolumeInformationA, (ULONG_PTR)GetVolumeInformationA );
            PatchAppImport( (ULONG_PTR)KERNEL32_GetDriveTypeA, (ULONG_PTR)GetDriveTypeA );
            PatchAppImport( (ULONG_PTR)USER32_ClipCursor, (ULONG_PTR)ClipCursor );
            PatchAppImport( (ULONG_PTR)GDI32_GetDeviceCaps, (ULONG_PTR)GetDeviceCaps );
            PatchAppImport( (ULONG_PTR)WINMM_mixerGetDevCapsA, (ULONG_PTR)mixerGetDevCapsA );
            PatchAppImport( (ULONG_PTR)KERNEL32_CreateSemaphoreA, (ULONG_PTR)CreateSemaphoreA );
            PatchAppImport( (ULONG_PTR)WINMM_waveOutGetDevCapsA, (ULONG_PTR)waveOutGetDevCapsA );
            PatchAppImport( (ULONG_PTR)KERNEL32_WaitForSingleObject, (ULONG_PTR)WaitForSingleObject );
            PatchAppImport( (ULONG_PTR)KERNEL32_ReleaseSemaphore, (ULONG_PTR)ReleaseSemaphore );
            PatchAppImport( (ULONG_PTR)GDI32_CreateBitmap, (ULONG_PTR)CreateBitmap );
            PatchAppImport( (ULONG_PTR)KERNEL32_SetUnhandledExceptionFilter, (ULONG_PTR)SetUnhandledExceptionFilter );
        }
        break;
    }
    return TRUE;
}
