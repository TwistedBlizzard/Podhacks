#ifdef _MSC_VER
#pragma warning( push, 1 )
#endif
#define CINTERFACE
#define COBJMACROS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#define WINVER 0x0600
#define _WIN32_IE WINVER
#define _WIN32_WINNT WINVER
#include <windows.h>
#include <commctrl.h>
#include <objbase.h>
#include <shellapi.h>
#include <shlwapi.h>
#include <mmsystem.h>
/** /
#include <mmddk.h>
/**/
#ifndef INITGUID
#define INITGUID
#endif
#ifndef DRV_QUERYFUNCTIONINSTANCEID
#define DRV_QUERYFUNCTIONINSTANCEID (DRV_RESERVED + 17)
#endif
#include <mmdeviceapi.h>
CLSID const CLSID_MMDeviceEnumerator = { 0xBCDE0395, 0xE52F, 0x467C, { 0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E } };
IID   const  IID_IMMDeviceEnumerator = { 0xA95664D2, 0x9614, 0x4F35, { 0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6 } };
#include <functiondiscoverykeys_devpkey.h>
#ifdef _MSC_VER
#pragma warning( pop )
#endif
#include "PodHacks.h"

/* Cursor resource */
#define IDC_MAIN 1
/* Icon resource */
#define IDI_MAIN 2
/* Main dialog resource */
#define IDD_MAIN 1
#define IDC_LIST 1001
#define IDC_CONF 1002
#define IDC_EXIT 1003
/* Settings dialog resource */
#define IDD_CONF 2
enum ConfigValue
{
    ConfigValue_Off,  /* BST_UNCHECKED */
    ConfigValue_On,   /* BST_CHECKED */
    ConfigValue_Auto  /* BST_INDETERMINATE */
};
struct ConfigValueEntry
{
    LPCSTR           Section;
    LPCSTR           Name;
    enum ConfigValue Default;
    BOOL             HasAuto;
    int              DlgItem;
}
const g_ConfigEntries[ 13 ] =
{
    { "PodHacks", "ValidateExe", ConfigValue_On,   FALSE, 1001 },
    { "POD",      "PatchCPUIDs", ConfigValue_On,   FALSE, 1002 },
    { "POD",      "BitsPixel16", ConfigValue_On,   FALSE, 1003 },
    { "POD",      "LooseCursor", ConfigValue_On,   FALSE, 1004 },
    { "POD",      "RedirectCDR", ConfigValue_Auto, TRUE,  1005 },
    #define IDC_NAME                                      1006
    { "POD",      "RedirectCDA", ConfigValue_Auto, TRUE,  1007 },
    #define IDC_WAVE                                      1008
    { "POD",      "RedirectWIN", ConfigValue_Auto, TRUE,  1009 },
    { "POD",      "NoResSwitch", ConfigValue_Auto, TRUE,  1010 },
    { "POD",      "ReverseMode", ConfigValue_On,   FALSE, 1011 },
    { "POD",      "DDFlipLimit", ConfigValue_On,   FALSE, 1012 },
    { "POD",      "DDFakeModes", ConfigValue_Off,  FALSE, 1013 },
    { "POD",      "NoSoundLock", ConfigValue_On,   FALSE, 1014 },
    { "GOG",      "HighResMode", ConfigValue_Auto, TRUE,  1015 }
};
#define IDC_DEFS 1016
#define IDC_LINK 1017
#define IDC_SAVE 1018
/* Player dialog resource */
#define IDD_CDA 3
/* Error messages */
#define IDS_ERROR_LIST_EMPTY 1
#define IDS_ERROR_OPEN_WRITE 2
#define IDS_ERROR_SET_FAILED 3
#define IDS_ERROR_NO_EXTRACT 4
#define IDS_ERROR_WRITE_CONF 5
/* Localized strings */
#define IDS_CD_PLAYER_DEVICE 16
/* Library resource */
#define IDX_FILE 1

/************************************************************************/
/* Globals                                                              */
/************************************************************************/

HINSTANCE g_Instance;
HANDLE g_PlayerMutex;
BOOL g_IsFillingList;
CHAR g_AppDirectory[ MAX_PATH ];

void FullAppFileName( LPSTR FullPath, LPCSTR FileName )
{
    lstrcpynA( FullPath, g_AppDirectory, MAX_PATH - lstrlenA( FileName ) );
    lstrcatA( FullPath, FileName );
}

int ErrorMessageBox( HWND Dlg, UINT ID, UINT Type, DWORD Error )
{
    CHAR Str[ 1024 ];
    if( 0 >= LoadStringA( g_Instance, ID, Str, 1024 ) )
        wsprintfA( Str, "ID: %d", ID );
    if( Error != 0 )
    {
        CHAR Err[ 1024 ];
        LPSTR Msg;
        if( 0 == FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_ALLOCATE_BUFFER, NULL, Error, 0, (LPSTR)&Msg, 0, NULL ) )
            Msg = 0;
        wsprintfA( Err, "%s\n\n0x%.8X (%d): %s", Str, Error, Error, Msg );
        if( Msg )
            LocalFree( Msg );
        lstrcpynA( Str, Err, 1024 );
    }
    return MessageBoxA( Dlg, Str, NULL, Type );
}

/************************************************************************/
/* Library extraction                                                   */
/************************************************************************/

DWORD ExtractLibrary( HWND Dlg )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.dll" );
    {
        HMODULE Module = LoadLibraryA( FullPath );
        if( Module )
        {
            DLLGETVERSIONPROC DllGetVersion = (DLLGETVERSIONPROC)GetProcAddress( Module, "DllGetVersion" );
            if( DllGetVersion )
            {
                DLLVERSIONINFO2 Version = { { sizeof(DLLVERSIONINFO2), 0, 0, 0, 0 }, 0, 0 };
                if( (S_OK == DllGetVersion( &Version.info1 )) &&
                    (Version.ullVersion >= MAKEDLLVERULL( PODHACKS_VER_MAJOR, PODHACKS_VER_MINOR, PODHACKS_VER_PATCH, 0 ) ) )
                {
                    FreeLibrary( Module );
                    return ERROR_SUCCESS;
                }
            }
            FreeLibrary( Module );
        }
    }
    {
        DWORD Error;
        HRSRC ResInfo = FindResourceA( NULL, MAKEINTRESOURCEA(IDX_FILE), RT_RCDATA );
        if( NULL == ResInfo )
            Error = GetLastError();
        else
        {
            HGLOBAL ResData = LoadResource( NULL, ResInfo );
            if( NULL == ResData )
                Error = GetLastError();
            else
            {
                LPVOID Data = LockResource( ResData );
                DWORD Size = SizeofResource( NULL, ResInfo );
                HANDLE File = CreateFileA( FullPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL );
                if( INVALID_HANDLE_VALUE == File )
                    Error = GetLastError();
                else
                {
                    DWORD Written;
                    if( FALSE == WriteFile( File, Data, Size, &Written, NULL ) )
                        Error = GetLastError();
                    else if( Written != Size )
                        Error = ERROR_WRITE_FAULT;
                    else
                        Error = ERROR_SUCCESS;
                    CloseHandle( File );
                }
            }
        }
        if( (Dlg != NULL) && (Error != ERROR_SUCCESS) )
            ErrorMessageBox( Dlg, IDS_ERROR_NO_EXTRACT, MB_ICONEXCLAMATION, Error );
        return Error;
    }
}

/************************************************************************/
/* Settings read/write                                                  */
/************************************************************************/

void GetConfigString( LPCSTR Section, LPCSTR Name, LPSTR Value, LPCSTR Default )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.ini" );
    GetPrivateProfileStringA( Section, Name, Default, Value, MAX_PATH, FullPath );
}

DWORD SetConfigString( LPCSTR Section, LPCSTR Name, LPCSTR Value )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.ini" );
    if( WritePrivateProfileStringA( Section, Name, Value, FullPath ) > 0 )
        return 0;
    return GetLastError();
}

int GetConfigInteger( LPCSTR Section, LPCSTR Name, int Default )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.ini" );
    return (int)GetPrivateProfileIntA( Section, Name, Default, FullPath );
}

DWORD SetConfigInteger( LPCSTR Section, LPCSTR Name, int Value )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "PodHacks.ini" );
    {
        CHAR String[ MAX_PATH ];
        wsprintfA( String, "%d", Value );
        return SetConfigString( Section, Name, String );
    }
}

enum ConfigValue GetConfigValue( int Entry )
{
    CHAR Value[ MAX_PATH ];
    GetConfigString( g_ConfigEntries[ Entry ].Section, g_ConfigEntries[ Entry ].Name, Value, NULL );
    if( (0 == lstrcmpiA( Value, "Off" )) || (0 == lstrcmpiA( Value, "False" )) || (0 == lstrcmpiA( Value, "0" )) )
        return ConfigValue_Off;
    if( (0 == lstrcmpiA( Value, "On" )) || (0 == lstrcmpiA( Value, "True" )) || (0 == lstrcmpiA( Value, "1" )) )
        return ConfigValue_On;
    if( (0 == lstrcmpiA( Value, "Auto" )) || (0 == lstrcmpiA( Value, "-1" )) )
        if( g_ConfigEntries[ Entry ].HasAuto )
            return ConfigValue_Auto;
    return g_ConfigEntries[ Entry ].Default;
}

DWORD SetConfigValue( int Entry, enum ConfigValue Value )
{
    LPCSTR const c_ConfigValueStrings[ 3 ] = { "Off", "On", "Auto" };
    return SetConfigString( g_ConfigEntries[ Entry ].Section, g_ConfigEntries[ Entry ].Name, c_ConfigValueStrings[ Value ] );
}

/************************************************************************/
/* Settings dialog                                                      */
/************************************************************************/

enum ConfigValue GetCheckState( HWND Dlg, int Item )
{
    return (enum ConfigValue)SendMessageA( GetDlgItem( Dlg, Item ), BM_GETCHECK, 0, 0 );
}

void SetCheckState( HWND Dlg, int Item, enum ConfigValue State )
{
    SendMessageA( GetDlgItem( Dlg, Item ), BM_SETCHECK, State, 0 );
}

INT_PTR CALLBACK DlgConf( HWND Dlg, UINT Msg, WPARAM wParam, LPARAM lParam )
{
    switch( Msg )
    {
    case WM_INITDIALOG:
        {
            int Entry;
            for( Entry = 0; Entry < ARRAYSIZE( g_ConfigEntries ); ++Entry )
            {
                SetCheckState( Dlg, g_ConfigEntries[ Entry ].DlgItem, GetConfigValue( Entry ) );
                switch( g_ConfigEntries[ Entry ].DlgItem + 1 )
                {
                case IDC_NAME:
                    {
                        HWND Name = GetDlgItem( Dlg, IDC_NAME );
                        SendMessageA( Name, CB_LIMITTEXT, MAX_PATH - 1, 0 );
                        SendMessageA( Name, CB_ADDSTRING, 0, (LPARAM)"POD" );
                        SendMessageA( Name, CB_ADDSTRING, 0, (LPARAM)"POD_POD_POD" );
                        SendMessageA( Name, CB_ADDSTRING, 0, (LPARAM)"POD2_0" );
                        {
                            CHAR Value[ MAX_PATH ];
                            GetConfigString( "POD", "VolumeLabel", Value, "POD2_0" );
                            SetWindowTextA( Name, Value );
                        }
                    }
                    break;

                case IDC_WAVE:
                    {
                        HWND Wave = GetDlgItem( Dlg, IDC_WAVE );
                        UINT Count = waveOutGetNumDevs();
                        {
                            CHAR Name[ MAX_PATH ];
                            if( 0 >= LoadStringA( g_Instance, IDS_CD_PLAYER_DEVICE, Name, MAX_PATH ) )
                                lstrcpynA( Name, "--player", MAX_PATH );
                            SendMessageA( Wave, CB_ADDSTRING, 0, (LPARAM)Name );
                        }
                        if( Count > 0 )
                        {
                            UINT ID;
                            for( ID = 0; ID < Count; ++ID )
                            {
                                {
                                    WCHAR StrId[ MAX_PATH ];
                                    if( MMSYSERR_NOERROR == waveOutMessage( (HWAVEOUT)ID, DRV_QUERYFUNCTIONINSTANCEID, (DWORD_PTR)StrId, MAX_PATH ) )
                                    {
                                        IMMDeviceEnumerator * Enumerator = NULL;
                                        if( SUCCEEDED(CoCreateInstance( &CLSID_MMDeviceEnumerator, NULL, CLSCTX_INPROC_SERVER, &IID_IMMDeviceEnumerator, (LPVOID *)&Enumerator )) )
                                        {
                                            IMMDevice * Device;
                                            if( SUCCEEDED(IMMDeviceEnumerator_GetDevice( Enumerator, StrId, &Device )) )
                                            {
                                                IPropertyStore * Properties;
                                                if( SUCCEEDED(IMMDevice_OpenPropertyStore( Device, STGM_READ, &Properties )) )
                                                {
                                                    PROPVARIANT Value = { VT_EMPTY, 0, 0, 0 }; Value.pwszVal = NULL;
                                                    if( SUCCEEDED(IPropertyStore_GetValue( Properties, &PKEY_Device_FriendlyName, &Value )) )
                                                    {
                                                        if( VT_LPWSTR == Value.vt )
                                                        {
                                                            CHAR Name[ MAX_PATH ];
                                                            BOOL UsedDefaultChar = FALSE;
                                                            if( (WideCharToMultiByte( CP_ACP, 0, Value.pwszVal, -1, Name, MAX_PATH, NULL, &UsedDefaultChar ) > 0) &&
                                                                (FALSE == UsedDefaultChar) )
                                                            {
                                                                SendMessageA( Wave, CB_ADDSTRING, 0, (LPARAM)Name );
                                                                PropVariantClear( &Value );
                                                                IPropertyStore_Release( Properties );
                                                                IMMDevice_Release( Device );
                                                                IMMDeviceEnumerator_Release( Enumerator );
                                                                continue;
                                                            }
                                                        }
                                                        PropVariantClear( &Value );
                                                    }
                                                    IPropertyStore_Release( Properties );
                                                }
                                                IMMDevice_Release( Device );
                                            }
                                            IMMDeviceEnumerator_Release( Enumerator );
                                        }
                                    }
                                }
                                {
                                    WAVEOUTCAPSA Caps;
                                    if( waveOutGetDevCapsA( ID, &Caps, sizeof(WAVEOUTCAPSA) ) != MMSYSERR_NOERROR )
                                        wsprintfA( Caps.szPname, "#%d", ID );
                                    SendMessageA( Wave, CB_ADDSTRING, 0, (LPARAM)Caps.szPname );
                                }
                            }
                        }
                        {
                            int Index = GetConfigInteger( "POD", "WavDeviceID", 0 );
                            if( (Index < 0) || ((UINT)Index > Count) )
                                Index = 0;
                            SendMessageA( Wave, CB_SETCURSEL, Index, 0 );
                        }
                    }
                    break;
                }
            }
        }
        return TRUE;

    case WM_NOTIFY:
        if( IDC_LINK == wParam )
        {
            PNMLINK Param = (PNMLINK)lParam;
            switch( Param->hdr.code )
            {
            case NM_CLICK:
            case NM_RETURN:
                {
                    HWND Link = GetDlgItem( Dlg, IDC_LINK );
                    EnableWindow( Link, FALSE );
                    {
                        CHAR File[ MAX_PATH ];
                        WideCharToMultiByte( CP_ACP, 0, Param->item.szUrl, -1, File, MAX_PATH, NULL, NULL );
                        ShellExecuteA( Dlg, "open", File, NULL, NULL, SW_SHOWNORMAL );
                    }
                    EnableWindow( Link, TRUE );
                    return TRUE;
                }
                break;
            }
        }
        break;

    case WM_COMMAND:
        if( BN_CLICKED == HIWORD( wParam ) )
        {
            switch( LOWORD( wParam ) )
            {
            case IDC_DEFS:
                {
                    int Entry;
                    for( Entry = 0; Entry < ARRAYSIZE( g_ConfigEntries ); ++Entry )
                    {
                        SetCheckState( Dlg, g_ConfigEntries[ Entry ].DlgItem, g_ConfigEntries[ Entry ].Default );
                        switch( g_ConfigEntries[ Entry ].DlgItem + 1 )
                        {
                        case IDC_NAME:
                            SetWindowTextA( GetDlgItem( Dlg, IDC_NAME ), "POD2_0" );
                            break;

                        case IDC_WAVE:
                            SendMessageA( GetDlgItem( Dlg, IDC_WAVE ), CB_SETCURSEL, 0, 0 );
                            break;
                        }
                    }
                }
                return TRUE;

            case IDC_SAVE:
                {
                    int Entry;
                    for( Entry = 0; Entry < ARRAYSIZE( g_ConfigEntries ); ++Entry )
                    {
                        DWORD Error = SetConfigValue( Entry, GetCheckState( Dlg, g_ConfigEntries[ Entry ].DlgItem ) );
                        if( 0 == Error )
                        {
                            switch( g_ConfigEntries[ Entry ].DlgItem + 1 )
                            {
                            case IDC_NAME:
                                {
                                    CHAR Value[ MAX_PATH ];
                                    GetWindowTextA( GetDlgItem( Dlg, IDC_NAME ), Value, MAX_PATH );
                                    Error = SetConfigString( "POD", "VolumeLabel", Value );
                                }
                                break;

                            case IDC_WAVE:
                                {
                                    int Value = (int)SendMessageA( GetDlgItem( Dlg, IDC_WAVE ), CB_GETCURSEL, 0, 0 );
                                    if( Value < 0 )
                                        Value = 0;
                                    Error = SetConfigInteger( "POD", "WavDeviceID", Value );
                                }
                                break;
                            }
                        }
                        if( Error != 0 )
                        {
                            ErrorMessageBox( Dlg, IDS_ERROR_WRITE_CONF, MB_ICONERROR, Error );
                            return TRUE;
                        }
                    }
                }
                EndDialog( Dlg, 0 );
                return TRUE;

            case IDCANCEL:
                EndDialog( Dlg, 0 );
                return TRUE;
            }
        }
        break;

    case WM_CLOSE:
        EndDialog( Dlg, 0 );
        return TRUE;
    }
    return FALSE;
}

/************************************************************************/
/* POD binary                                                           */
/************************************************************************/

BOOL IsValidPodExe( LPCSTR FileName )
{
    HMODULE Module = LoadLibraryExA( FileName, NULL, LOAD_LIBRARY_AS_DATAFILE );
    if( Module )
    {
        HRSRC ResInfo = FindResourceA( Module, "DLGINCLUDE", RT_RCDATA );
        if( ResInfo )
        {
            HGLOBAL ResData = LoadResource( Module, ResInfo );
            if( ResData )
            {
                LPCSTR DlgInclude = LockResource( ResData );
                if( 0 == lstrcmpiA( DlgInclude, "c:\\POD\\pod.h" ) )
                {
                    FreeLibrary( Module );
                    return TRUE;
                }
            }
        }
        FreeLibrary( Module );
    }
    return FALSE;
}

struct PodExe_t
{
    HANDLE                File;
    DWORD                 Error;
    IMAGE_NT_HEADERS32    Headers;
    PIMAGE_SECTION_HEADER Sections;
};

void ClosePodExe( struct PodExe_t * PodExe )
{
    if( PodExe->File != INVALID_HANDLE_VALUE )
    {
        CloseHandle( PodExe->File );
        PodExe->File = INVALID_HANDLE_VALUE;
    }
    if( PodExe->Sections != NULL )
    {
        VirtualFree( PodExe->Sections, 0, MEM_RELEASE );
        PodExe->Sections = NULL;
    }
}

BOOL ReadPodExe( struct PodExe_t * PodExe, LPVOID Data, DWORD Size )
{
    DWORD Read;
    if( FALSE == ReadFile( PodExe->File, Data, Size, &Read, NULL ) )
    {
        PodExe->Error = GetLastError();
        return FALSE;
    }
    if( Read != Size )
    {
        PodExe->Error = ERROR_HANDLE_EOF;
        return FALSE;
    }
    return TRUE;
}

BOOL WritePodExe( struct PodExe_t * PodExe, LPCVOID Data, DWORD Size )
{
    DWORD Written;
    if( FALSE == WriteFile( PodExe->File, Data, Size, &Written, NULL ) )
    {
        PodExe->Error = GetLastError();
        return FALSE;
    }
    if( Written != Size )
    {
        PodExe->Error = ERROR_WRITE_FAULT;
        return FALSE;
    }
    return TRUE;
}

BOOL SeekPodExe( struct PodExe_t * PodExe, DWORD VirtualAddress )
{
    WORD Index;
    for( Index = 0; Index < PodExe->Headers.FileHeader.NumberOfSections; ++Index )
    {
        PIMAGE_SECTION_HEADER Section = &(PodExe->Sections[ Index ]);
        if( VirtualAddress >= Section->VirtualAddress )
        {
            DWORD Offset = VirtualAddress - Section->VirtualAddress;
            if( Offset < Section->SizeOfRawData )
            {
                if( INVALID_SET_FILE_POINTER == SetFilePointer( PodExe->File, Section->PointerToRawData + Offset, NULL, FILE_BEGIN ) )
                {
                    PodExe->Error = GetLastError();
                    return FALSE;
                }
                else
                {
                    PodExe->Error = 0;
                    return TRUE;
                }
            }
        }
    }
    PodExe->Error = ERROR_INVALID_ADDRESS;
    return FALSE;
}


BOOL OpenPodExe( struct PodExe_t * PodExe, LPCSTR FileName, DWORD Access, DWORD Share )
{
    BOOL Result = FALSE;
    PodExe->Sections = NULL;
    PodExe->File = CreateFileA( FileName, Access, Share, NULL, OPEN_EXISTING, 0, NULL );
    if( INVALID_HANDLE_VALUE == PodExe->File )
        PodExe->Error = GetLastError();
    else
    {
        IMAGE_DOS_HEADER DosHeader;
        if( (FALSE == ReadPodExe( PodExe, &DosHeader, sizeof(IMAGE_DOS_HEADER) )) ||
            (DosHeader.e_magic != IMAGE_DOS_SIGNATURE) ||
            (DosHeader.e_lfanew <= 0) ||
            (INVALID_SET_FILE_POINTER == SetFilePointer( PodExe->File, DosHeader.e_lfanew, NULL, FILE_BEGIN )) ||
            (FALSE == ReadPodExe( PodExe, &(PodExe->Headers), sizeof(IMAGE_NT_HEADERS32) )) ||
            (PodExe->Headers.Signature != IMAGE_NT_SIGNATURE) ||
            (PodExe->Headers.FileHeader.Machine != IMAGE_FILE_MACHINE_I386) ||
            (0 == PodExe->Headers.FileHeader.NumberOfSections) ||
            (PodExe->Headers.FileHeader.SizeOfOptionalHeader < FIELD_OFFSET(IMAGE_OPTIONAL_HEADER32, DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT + 1 ])) ||
            (INVALID_SET_FILE_POINTER == SetFilePointer( PodExe->File, (LONG)PodExe->Headers.FileHeader.SizeOfOptionalHeader - sizeof(IMAGE_OPTIONAL_HEADER32), NULL, FILE_CURRENT )) )
        {
            PodExe->Error = ERROR_BAD_FORMAT;
        }
        else
        {
            PodExe->Sections = VirtualAlloc( NULL, PodExe->Headers.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
            if( NULL == PodExe->Sections )
                PodExe->Error = GetLastError();
            else
            {
                if( FALSE == ReadPodExe( PodExe, PodExe->Sections, PodExe->Headers.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) ) )
                    PodExe->Error = ERROR_BAD_FORMAT;
                else
                {
                    PodExe->Error = 0;
                    Result = TRUE;
                }
            }
        }
    }
    if( FALSE == Result )
        ClosePodExe( PodExe );
    return Result;
}

UINT GetExeState( LPCSTR FileName )
{
    UINT State = 0;
    struct PodExe_t PodExe;
    if( OpenPodExe( &PodExe, FileName, GENERIC_READ, FILE_SHARE_READ ) )
    {
        DWORD Size = PodExe.Headers.OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].Size;
        if( (Size >= sizeof(IMAGE_IMPORT_DESCRIPTOR)) &&
            SeekPodExe( &PodExe, PodExe.Headers.OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].VirtualAddress ) )
        {
            PIMAGE_IMPORT_DESCRIPTOR Imports = VirtualAlloc( NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
            if( Imports )
            {
                if( ReadPodExe( &PodExe, Imports, Size ) )
                {
                    DWORD Index;
                    for( Index = 0; Index < Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++Index )
                    {
                        CHAR Name[ sizeof("PodHacks.dll") ];
                        if( SeekPodExe( &PodExe, Imports[ Index ].Name ) &&
                            ReadPodExe( &PodExe, Name, sizeof(Name) ) )
                        {
                            Name[ sizeof(Name) - 1 ] = '\0';
                            if( 0 == lstrcmpiA( Name, "AVIFIL32.dll" ) )
                            {
                                State = INDEXTOSTATEIMAGEMASK(1);
                                break;
                            }
                            else if( 0 == lstrcmpiA( Name, "PodHacks.dll" ) )
                                State = INDEXTOSTATEIMAGEMASK(2);
                        }
                    }
                }
                VirtualFree( Imports, 0, MEM_RELEASE );
            }
        }
        ClosePodExe( &PodExe );
    }
    return State;
}

BOOL SetExeState( struct PodExe_t * PodExe, UINT State )
{
    BOOL Result = FALSE;
    DWORD Size = PodExe->Headers.OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].Size;
    if( Size < sizeof(IMAGE_IMPORT_DESCRIPTOR) )
        PodExe->Error = ERROR_BAD_FORMAT;
    else if( SeekPodExe( PodExe, PodExe->Headers.OptionalHeader.DataDirectory[ IMAGE_DIRECTORY_ENTRY_IMPORT ].VirtualAddress ) )
    {
        PIMAGE_IMPORT_DESCRIPTOR Imports = VirtualAlloc( NULL, Size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE );
        if( NULL == Imports )
            PodExe->Error = GetLastError();
        else
        {
            if( ReadPodExe( PodExe, Imports, Size ) )
            {
                DWORD Index;
                PodExe->Error = ERROR_NOT_FOUND;
                for( Index = 0; Index < Size / sizeof(IMAGE_IMPORT_DESCRIPTOR); ++Index )
                {
                    CHAR Name[ sizeof("PodHacks.dll") ];
                    if( SeekPodExe( PodExe, Imports[ Index ].Name ) &&
                        ReadPodExe( PodExe, Name, sizeof(Name) ) )
                    {
                        Name[ sizeof(Name) - 1 ] = '\0';
                        if( INDEXTOSTATEIMAGEMASK(1) == (LVIS_STATEIMAGEMASK & State) )
                        {
                            if( 0 == lstrcmpiA( Name, "AVIFIL32.dll" ) )
                                Result = TRUE;
                            else if( 0 == lstrcmpiA( Name, "PodHacks.dll" ) )
                            {
                                lstrcpynA( Name, "AVIFIL32.dll", sizeof(Name) );
                                SeekPodExe( PodExe, Imports[ Index ].Name );
                                Result = WritePodExe( PodExe, Name, sizeof(Name) );
                                if( FALSE == Result )
                                    break;
                            }
                        }
                        else if( INDEXTOSTATEIMAGEMASK(2) == (LVIS_STATEIMAGEMASK & State) )
                        {
                            if( 0 == lstrcmpiA( Name, "PodHacks.dll" ) )
                                Result = TRUE;
                            else if( 0 == lstrcmpiA( Name, "AVIFIL32.dll" ) )
                            {
                                lstrcpynA( Name, "PodHacks.dll", sizeof(Name) );
                                SeekPodExe( PodExe, Imports[ Index ].Name );
                                Result = WritePodExe( PodExe, Name, sizeof(Name) );
                                if( FALSE == Result )
                                    break;
                            }
                        }
                        else
                        {
                            PodExe->Error = ERROR_INTERNAL_ERROR;
                            Result = FALSE;
                            break;
                        }
                    }
                }
            }
            VirtualFree( Imports, 0, MEM_RELEASE );
        }
    }
    return Result;
}

/************************************************************************/
/* main dialog                                                          */
/************************************************************************/

void FillList( HWND Dlg, HWND List )
{
    CHAR FullPath[ MAX_PATH ];
    FullAppFileName( FullPath, "*.exe" );
    g_IsFillingList = TRUE;
    {
        while( (int)SendMessageA( List, LVM_GETITEMCOUNT, 0, 0 ) > 0 )
            SendMessageA( List, LVM_DELETEITEM, 0, 0 );
    }
    {
        WIN32_FIND_DATAA FindData;
        HANDLE FindFile = FindFirstFileA( FullPath, &FindData );
        if( FindFile != INVALID_HANDLE_VALUE )
        {
            BOOL NoCheck = (ConfigValue_Off == GetConfigValue( 0 ));
            BOOL Extract = FALSE;
            do
            {
                if( 0 == (FILE_ATTRIBUTE_DIRECTORY & FindData.dwFileAttributes) )
                {
                    FullAppFileName( FullPath, FindData.cFileName );
                    if( NoCheck || IsValidPodExe( FullPath ) )
                    {
                        LVITEMA Item = { LVIF_TEXT, 0, 0, 0, 0, NULL, 0, 0, 0 };
                        Item.state = GetExeState( FullPath );
                        Item.stateMask = LVIS_STATEIMAGEMASK;
                        Item.pszText = FindData.cFileName;
                        if( Item.state != 0 )
                        {
                            int Index = SendMessageA( List, LVM_INSERTITEMA, 0, (LPARAM)&Item );
                            if( Index >= 0 )
                            {
                                if( INDEXTOSTATEIMAGEMASK(2) == (LVIS_STATEIMAGEMASK & Item.state) )
                                    Extract = TRUE;
                                Item.mask = LVIF_STATE;
                                SendMessageA( List, LVM_SETITEMSTATE, Index, (LPARAM)&Item );
                            }
                        }
                    }
                }
            }
            while( FindNextFileA( FindFile, &FindData ));
            if( Extract )
                ExtractLibrary( Dlg );
        }
        g_IsFillingList = FALSE;
    }
}

INT_PTR CALLBACK DlgMain( HWND Dlg, UINT Msg, WPARAM wParam, LPARAM lParam )
{
    switch( Msg )
    {
    case WM_INITDIALOG:
        SendMessageA( Dlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconA( g_Instance, MAKEINTRESOURCEA(IDI_MAIN) ) );
        {
            CHAR Caption[ MAX_PATH ];
            GetWindowTextA( Dlg, Caption, MAX_PATH - sizeof("( " PODHACKS_VER_STRING ")") );
            lstrcatA( Caption, " (" PODHACKS_VER_STRING ")" );
            SetWindowTextA( Dlg, Caption );
        }
        {
            HWND List = GetDlgItem( Dlg, IDC_LIST );
            LONG_PTR Style = LVS_EX_TWOCLICKACTIVATE | LVS_EX_FULLROWSELECT | LVS_EX_TRACKSELECT | LVS_EX_CHECKBOXES;
            SendMessageA( List, LVM_SETEXTENDEDLISTVIEWSTYLE, 0, Style );
            SendMessageA( List, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_INFOTIP, LVS_EX_INFOTIP | Style );
            SendMessageA( List, LVM_SETEXTENDEDLISTVIEWSTYLE, LVS_EX_DOUBLEBUFFER, LVS_EX_DOUBLEBUFFER | Style );
            SendMessageA( List, LVM_SETHOVERTIME, 0, 1 );
            SendMessageA( List, LVM_SETHOTCURSOR, 0, (LPARAM)LoadCursorA( g_Instance, MAKEINTRESOURCEA(IDC_MAIN) ) );
            {
                HFONT Font = (HFONT)SendMessageA( Dlg, WM_GETFONT, 0, 0 );
                if( Font )
                {
                    LOGFONTA ListFont;
                    if( sizeof(LOGFONTA) == GetObjectA( Font, sizeof(LOGFONTA), &ListFont ) )
                    {
                        ListFont.lfHeight += (ListFont.lfHeight >= 0) ? +2 : -2;
                        ListFont.lfWidth = 0;
                        Font = CreateFontIndirectA( &ListFont );
                        if( Font )
                            SendMessageA( List, WM_SETFONT, (WPARAM)Font, FALSE );
                    }
                }
            }
            {
                RECT Rect;
                LVCOLUMNA Column = { LVCF_WIDTH, 0, 0, NULL, 0 };
                GetClientRect( List, &Rect );
                Column.cx = Rect.right - Rect.left - GetSystemMetrics( SM_CXVSCROLL );
                SendMessageA( List, LVM_INSERTCOLUMNA, 0, (LPARAM)&Column );
            }
            FillList( Dlg, List );
            if( (int)SendMessageA( List, LVM_GETITEMCOUNT, 0, 0 ) <= 0 )
                ErrorMessageBox( Dlg, IDS_ERROR_LIST_EMPTY, MB_ICONEXCLAMATION, 0 );
            else
            {
                LVITEMA Item = { LVIF_STATE, 0, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED, NULL, 0, 0, 0 };
                SendMessageA( List, LVM_SETITEMSTATE, 0, (LPARAM)&Item );
            }
        }
        return TRUE;

    case WM_NOTIFY:
        if( IDC_LIST == wParam )
        {
            LPNMLISTVIEW Param = (LPNMLISTVIEW)lParam;
            switch( Param->hdr.code )
            {
            case LVN_ITEMACTIVATE:
                {
                    HWND List = GetDlgItem( Dlg, IDC_LIST );
                    int Index = (int)SendMessageA( List, LVM_GETSELECTIONMARK, 0, 0 );
                    if( Index >= 0 )
                    {
                        LVITEMA Item = { LVIF_STATE, 0, 0, 0, LVIS_STATEIMAGEMASK, NULL, 0, 0, 0 };
                        Item.state = INDEXTOSTATEIMAGEMASK(1 | 2) ^ (INDEXTOSTATEIMAGEMASK(1 | 2) &
                            (UINT)SendMessageA( List, LVM_GETITEMSTATE, Index, LVIS_STATEIMAGEMASK ));
                        SendMessageA( List, LVM_SETITEMSTATE, Index, (LPARAM)&Item );
                        return TRUE;
                    }
                }
                break;

            case LVN_ITEMCHANGING:
                if( (FALSE == g_IsFillingList) &&
                    ((LVIF_STATE & Param->uChanged) != 0) &&
                    ((LVIS_STATEIMAGEMASK & Param->uOldState) != 0) && ((LVIS_STATEIMAGEMASK & Param->uNewState) != 0) &&
                    ((LVIS_STATEIMAGEMASK & Param->uOldState) != (LVIS_STATEIMAGEMASK & Param->uNewState)) )
                {
                    LVITEMA Item = { LVIF_STATE, 0, 0, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED, NULL, 0, 0, 0 };
                    HWND List = GetDlgItem( Dlg, IDC_LIST );
                    SendMessageA( List, LVM_SETITEMSTATE, Param->iItem, (LPARAM)&Item );
                    {
                        BOOL PreventChange = TRUE;
                        {
                            CHAR FileName[ MAX_PATH ];
                            Item.mask = LVIF_TEXT;
                            Item.pszText = FileName;
                            Item.cchTextMax = MAX_PATH;
                            SendMessageA( List, LVM_GETITEMTEXTA, Param->iItem, (LPARAM)&Item );
                            {
                                CHAR FullPath[ MAX_PATH ];
                                struct PodExe_t PodExe;
                                FullAppFileName( FullPath, FileName );
                                if( FALSE == OpenPodExe( &PodExe, FullPath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ ) )
                                    ErrorMessageBox( Dlg, IDS_ERROR_OPEN_WRITE, MB_ICONERROR, PodExe.Error );
                                else
                                {
                                    if( FALSE == SetExeState( &PodExe, LVIS_STATEIMAGEMASK & Param->uNewState ) )
                                        ErrorMessageBox( Dlg, IDS_ERROR_SET_FAILED, MB_ICONERROR, PodExe.Error );
                                    else
                                    {
                                        if( INDEXTOSTATEIMAGEMASK(2) == (LVIS_STATEIMAGEMASK & Param->uNewState) )
                                            ExtractLibrary( Dlg );
                                        PreventChange = FALSE;
                                    }
                                    ClosePodExe( &PodExe );
                                }
                            }
                        }
                        SetWindowLongA( Dlg, DWL_MSGRESULT, PreventChange );
                        return TRUE;
                    }
                }
                break;
            }
        }
        break;

    case WM_COMMAND:
        if( BN_CLICKED == HIWORD( wParam ) )
        {
            switch( LOWORD( wParam ) )
            {
            case IDC_CONF:
                {
                    BOOL NoCheck = (ConfigValue_Off == GetConfigValue( 0 ));
                    DialogBoxParamA( g_Instance, MAKEINTRESOURCEA(IDD_CONF), Dlg, DlgConf, 0 );
                    if( NoCheck != (ConfigValue_Off == GetConfigValue( 0 )) )
                        FillList( Dlg, GetDlgItem( Dlg, IDC_LIST ) );
                }
                return TRUE;

            case IDCANCEL:
            case IDC_EXIT:
                EndDialog( Dlg, 0 );
                return TRUE;
            }
        }
        break;

    case WM_CLOSE:
        EndDialog( Dlg, 0 );
        return TRUE;
    }
    return FALSE;
}

/************************************************************************/
/* CD Audio dialog                                                      */
/************************************************************************/

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

INT_PTR CALLBACK DlgCDA( HWND Dlg, UINT Msg, WPARAM wParam, LPARAM lParam )
{
    static MCIDEVICEID s_DeviceID;
    static DWORD_PTR s_Callback;

    switch( Msg )
    {
    case WM_INITDIALOG:
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
        {
            typedef BOOL (WINAPI * PFNCHANGEWINDOWMESSAGEFILTER)( UINT message, DWORD dwFlag);
            PFNCHANGEWINDOWMESSAGEFILTER MessageFilter = (PFNCHANGEWINDOWMESSAGEFILTER)
                GetProcAddress( GetModuleHandle( "USER32" ), "ChangeWindowMessageFilter" );
            if( MessageFilter )
            {
                MessageFilter( g_PlayerMessage.Find, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.Open, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.SetTimeFormat, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.GetItemLength, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.SetCallback, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.Play, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.Stop, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.Close, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.Pause, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.Resume, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.GetVolume, MSGFLT_ADD );
                MessageFilter( g_PlayerMessage.SetVolume, MSGFLT_ADD );
            }
        }
        SetClassLongA( Dlg, GCL_STYLE, CS_NOCLOSE | GetClassLongA( Dlg, GCL_STYLE ) );
        EnableMenuItem( GetSystemMenu( Dlg, FALSE ), SC_CLOSE, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED );
        SendMessageA( Dlg, WM_SETICON, ICON_BIG, (LPARAM)LoadIconA( g_Instance, MAKEINTRESOURCEA(IDI_MAIN) ) );
        SetTimer( Dlg, 1, USER_TIMER_MINIMUM, 0 );
        return TRUE;

    case WM_WINDOWPOSCHANGING:
        {
            LPWINDOWPOS WindowPos = (LPWINDOWPOS)lParam;
            if( WindowPos->flags & SWP_SHOWWINDOW )
                WindowPos->flags |= SWP_NOACTIVATE;
        }
        break;

    case WM_MOUSEACTIVATE:
        {
            SetWindowLongA( Dlg, DWL_MSGRESULT, MA_NOACTIVATE );
            return TRUE;
        }
        break;

    case WM_TIMER:
        if( 1 == wParam )
        {
            switch( WaitForSingleObject( g_PlayerMutex, 0 ) )
            {
            case WAIT_OBJECT_0:
            case WAIT_ABANDONED:
                ReleaseMutex( g_PlayerMutex );
            default:
                EndDialog( Dlg, 0 );
            case WAIT_TIMEOUT:
                break;
            }
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog( Dlg, 0 );
        return TRUE;

    case WM_DESTROY:
        KillTimer( Dlg, 1 );
        break;

    default:
        if( g_PlayerMessage.Find && (Msg == g_PlayerMessage.Find) )
        {
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)MAKELONG( PODHACKS_VER_MINOR, PODHACKS_VER_MAJOR ) );
            return TRUE;
        }
        else if( g_PlayerMessage.Open && (Msg == g_PlayerMessage.Open) )
        {
            MCIERROR Status;
            CHAR FullPath[ MAX_PATH ];
            MCI_WAVE_OPEN_PARMSA Open;
            FullAppFileName( FullPath, "Track02.wav" );
            Open.dwCallback = 0;
            Open.wDeviceID = 0;
            Open.lpstrDeviceType = (LPCSTR)MCI_DEVTYPE_WAVEFORM_AUDIO;
            Open.lpstrElementName = FullPath;
            Open.lpstrAlias = NULL;
            Open.dwBufferSeconds = 0;
            Status = mciSendCommandA( 0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_TYPE_ID | MCI_OPEN_ELEMENT | MCI_WAIT, (DWORD_PTR)&Open );
            if( Status != MMSYSERR_NOERROR )
            {
                FullAppFileName( FullPath, "Track02.mp3" );
                Open.lpstrDeviceType = "MPEGVideo";
                Status = mciSendCommandA( 0, MCI_OPEN, MCI_OPEN_TYPE | MCI_OPEN_ELEMENT | MCI_WAIT, (DWORD_PTR)&Open );
            }
            if( MMSYSERR_NOERROR == Status )
                s_DeviceID = Open.wDeviceID;
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.SetTimeFormat && (Msg == g_PlayerMessage.SetTimeFormat) )
        {
            MCIERROR Status;
            if( (DWORD)wParam != MCI_FORMAT_TMSF )
                Status = MCIERR_BAD_TIME_FORMAT;
            else if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
            {
                MCI_SET_PARMS Set = { 0, MCI_FORMAT_MILLISECONDS, 0 };
                Status = mciSendCommandA( s_DeviceID, MCI_SET, MCI_SET_TIME_FORMAT, (DWORD_PTR)&Set );
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.GetItemLength && (Msg == g_PlayerMessage.GetItemLength) )
        {
            DWORD_PTR Length = 0;
            if( s_DeviceID )
            {
                MCI_STATUS_PARMS Status = { 0, 0, MCI_STATUS_LENGTH, 0 };
                if( MMSYSERR_NOERROR == mciSendCommandA( s_DeviceID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&Status ) )
                    Length = MCI_MAKE_MSF(
                        Status.dwReturn / 60000,
                        (Status.dwReturn % 60000) / 1000,
                        (Status.dwReturn % 1000) * 75 / 1000 );
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Length );
            return TRUE;
        }
        else if( g_PlayerMessage.SetCallback && (Msg == g_PlayerMessage.SetCallback) )
        {
            MCIERROR Status;
            if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
            {
                s_Callback = (DWORD_PTR)wParam;
                Status = MMSYSERR_NOERROR;
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.Play && (Msg == g_PlayerMessage.Play) )
        {
            MCIERROR Status;
            if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
            {
                MCI_PLAY_PARMS Play;
                Play.dwCallback = s_Callback;
                Play.dwFrom = (DWORD)wParam;
                Play.dwTo = (DWORD)lParam;
                if( MCI_TMSF_TRACK( Play.dwTo ) > MCI_TMSF_TRACK( Play.dwFrom ) )
                {
                    MCI_STATUS_PARMS Item = { 0, 0, MCI_STATUS_LENGTH, 0 };
                    Status = mciSendCommandA( s_DeviceID, MCI_STATUS, MCI_STATUS_ITEM, (DWORD_PTR)&Item );
                    if( Status != MMSYSERR_NOERROR )
                    {
                        SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
                        return TRUE;
                    }
                    Play.dwTo = Item.dwReturn;
                }
                else
                {
                    Play.dwTo =
                        (MCI_TMSF_FRAME( Play.dwTo ) * 1000 / 75) +
                        (MCI_TMSF_SECOND( Play.dwTo ) * 1000) +
                        (MCI_TMSF_MINUTE( Play.dwTo ) * 60000);
                }
                Play.dwFrom =
                    (MCI_TMSF_FRAME( Play.dwFrom ) * 1000 / 75) +
                    (MCI_TMSF_SECOND( Play.dwFrom ) * 1000) +
                    (MCI_TMSF_MINUTE( Play.dwFrom ) * 60000);
                {
                    DWORD_PTR Flags = MCI_FROM | MCI_TO;
                    if( Play.dwCallback )
                        Flags |= MCI_NOTIFY;
                    Status = mciSendCommandA( s_DeviceID, MCI_PLAY, Flags, (DWORD_PTR)&Play );
                }
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.Stop && (Msg == g_PlayerMessage.Stop) )
        {
            MCIERROR Status;
            if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
            {
                MCI_GENERIC_PARMS Stop;
                Stop.dwCallback = s_Callback;
                {
                    DWORD_PTR Flags = (Stop.dwCallback) ? MCI_NOTIFY : 0;
                    Status = mciSendCommandA( s_DeviceID, MCI_STOP, Flags, (DWORD_PTR)&Stop );
                }
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.Close && (Msg == g_PlayerMessage.Close) )
        {
            MCIERROR Status;
            if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
            {
                Status = mciSendCommandA( s_DeviceID, MCI_CLOSE, MCI_WAIT, 0 );
                if( MMSYSERR_NOERROR == Status )
                {
                    s_DeviceID = 0;
                    s_Callback = 0;
                }
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.Pause && (Msg == g_PlayerMessage.Pause) )
        {
            MCIERROR Status;
            if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
                Status = mciSendCommandA( s_DeviceID, MCI_PAUSE, MCI_WAIT, 0 );
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.Resume && (Msg == g_PlayerMessage.Resume) )
        {
            MCIERROR Status;
            if( 0 == s_DeviceID )
                Status = MCIERR_DEVICE_NOT_READY;
            else
            {
                MCI_PLAY_PARMS Play;
                Play.dwCallback = s_Callback;
                Play.dwFrom = 0;
                Play.dwTo = (DWORD)wParam;
                Play.dwTo =
                    (MCI_TMSF_FRAME( Play.dwTo ) * 1000 / 75) +
                    (MCI_TMSF_SECOND( Play.dwTo ) * 1000) +
                    (MCI_TMSF_MINUTE( Play.dwTo ) * 60000);
                {
                    DWORD_PTR Flags = MCI_FROM | MCI_TO;
                    if( Play.dwCallback )
                        Flags |= MCI_NOTIFY;
                    Status = mciSendCommandA( s_DeviceID, MCI_PLAY, Flags, (DWORD_PTR)&Play );
                }
            }
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        else if( g_PlayerMessage.GetVolume && (Msg == g_PlayerMessage.GetVolume) )
        {
            DWORD Volume = 0;
            if( waveOutGetVolume( 0, &Volume ) != MMSYSERR_NOERROR )
                Volume = 0;
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Volume );
            return TRUE;
        }
        else if( g_PlayerMessage.SetVolume && (Msg == g_PlayerMessage.SetVolume) )
        {
            DWORD Volume = (DWORD)wParam;
            MCIERROR Status = waveOutSetVolume( 0, Volume );
            SetWindowLongA( Dlg, DWL_MSGRESULT, (LONG)Status );
            return TRUE;
        }
        break;
    }
    return FALSE;
}

/************************************************************************/
/* Entry point                                                          */
/************************************************************************/

#ifdef _VC_NODEFAULTLIB
    #define RETURN_EXITCODE( ExitCode ) ExitProcess( ExitCode )
#else
    #define RETURN_EXITCODE( ExitCode ) return ExitCode
#endif

int WINAPI WinMain( HINSTANCE Instance, HINSTANCE PrevInstance, LPSTR CmdLine, int CmdShow )
{
    UNREFERENCED_PARAMETER( Instance );
    UNREFERENCED_PARAMETER( PrevInstance );
    UNREFERENCED_PARAMETER( CmdShow );
    g_Instance = GetModuleHandle( NULL );
    {
        GetModuleFileNameA( g_Instance, g_AppDirectory, MAX_PATH );
        {
            LPSTR FileName = &g_AppDirectory[ lstrlenA( g_AppDirectory ) ];
            while( (FileName > g_AppDirectory) && (FileName[ -1 ] != '\\') && (FileName[ -1 ] != '/') )
                --FileName;
            *FileName = '\0';
        }
    }
    CoInitialize( NULL );
    CmdLine = GetCommandLineA();
    if( CmdLine && (*CmdLine != '\0') )
    {
        LPSTR CmdLineEnd = &CmdLine[ lstrlenA( CmdLine ) ];
        while( (CmdLineEnd > CmdLine) && (' ' == CmdLineEnd[ -1 ]) )
            *(--CmdLineEnd) = '\0';
        if( (*CmdLine != '\0') && (CmdLine[ 0 ] != '-') && (CmdLine[ 1 ] != '-') )
        {
            CHAR CmdTerm = *CmdLine;
            if( CmdTerm != '"' )
                CmdTerm = ' ';
            ++CmdLine;
            while( (*CmdLine != '\0') && (*CmdLine != CmdTerm) )
                ++CmdLine;
            ++CmdLine;
            while( ' ' == *CmdLine )
                ++CmdLine;
        }
    }
    if( 0 == lstrcmpiA( CmdLine, "--install" ) )
    {
        CHAR FileName[ MAX_PATH ];
        HANDLE FindFile;
        WIN32_FIND_DATAA FindData;
        FullAppFileName( FileName, "*.exe" );
        FindFile = FindFirstFileA( FileName, &FindData );
        if( INVALID_HANDLE_VALUE == FindFile )
        {
            CoUninitialize();
            RETURN_EXITCODE( 1 );
        }
        {
            BOOL Extract = FALSE;
            do
            {
                FullAppFileName( FileName, FindData.cFileName );
                if( (0 == (FILE_ATTRIBUTE_DIRECTORY & FindData.dwFileAttributes)) && IsValidPodExe( FileName ) )
                    switch( LVIS_STATEIMAGEMASK & GetExeState( FileName ) )
                    {
                    case INDEXTOSTATEIMAGEMASK(1):
                        {
                            struct PodExe_t PodExe;
                            if( FALSE == OpenPodExe( &PodExe, FileName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ ) )
                            {
                                FindClose( FindFile );
                                CoUninitialize();
                                RETURN_EXITCODE( 2 );
                            }
                            if( FALSE == SetExeState( &PodExe, INDEXTOSTATEIMAGEMASK(2) ) )
                            {
                                ClosePodExe( &PodExe );
                                FindClose( FindFile );
                                CoUninitialize();
                                RETURN_EXITCODE( 3 );
                            }
                            ClosePodExe( &PodExe );
                        }
                        /* fall-through */
                    case INDEXTOSTATEIMAGEMASK(2):
                        Extract = TRUE;
                        break;
                    }
            }
            while( FindNextFileA( FindFile, &FindData ));
            FindClose( FindFile );
            if( FALSE == Extract )
            {
                CoUninitialize();
                RETURN_EXITCODE( 4 );
            }
            if( ExtractLibrary( NULL ) != ERROR_SUCCESS )
            {
                CoUninitialize();
                RETURN_EXITCODE( 5 );
            }
        }
    }
    else if( 0 == lstrcmpiA( CmdLine, "--player" ) )
    {
        g_PlayerMutex = OpenMutexA( SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE, "PodHacks_Player" );
        if( g_PlayerMutex )
        {
            InitCommonControls();
            {
                INITCOMMONCONTROLSEX Controls = { sizeof(INITCOMMONCONTROLSEX), ICC_STANDARD_CLASSES };
                InitCommonControlsEx( &Controls );
            }
            DialogBoxParamA( g_Instance, MAKEINTRESOURCEA(IDD_CDA), NULL, DlgCDA, 0 );
            CloseHandle( g_PlayerMutex );
        }
    }
    else
    {
        InitCommonControls();
        {
            INITCOMMONCONTROLSEX Controls = { sizeof(INITCOMMONCONTROLSEX), ICC_LISTVIEW_CLASSES | ICC_TAB_CLASSES | ICC_STANDARD_CLASSES | ICC_LINK_CLASS };
            InitCommonControlsEx( &Controls );
        }
        DialogBoxParamA( g_Instance, MAKEINTRESOURCEA(IDD_MAIN), NULL, DlgMain, 0 );
    }
    CoUninitialize();
    RETURN_EXITCODE( 0 );
}

