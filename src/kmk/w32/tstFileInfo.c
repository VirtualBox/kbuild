
#include <stdio.h>
#include <Windows.h>

typedef enum _FILE_INFORMATION_CLASS {
    FileDirectoryInformation       = 1,
    FileFullDirectoryInformation,   // 2
    FileBothDirectoryInformation,   // 3
    FileBasicInformation,           // 4  wdm
    FileStandardInformation,        // 5  wdm
    FileInternalInformation,        // 6
    FileEaInformation,              // 7
    FileAccessInformation,          // 8
    FileNameInformation,            // 9
    FileRenameInformation,          // 10
    FileLinkInformation,            // 11
    FileNamesInformation,           // 12
    FileDispositionInformation,     // 13
    FilePositionInformation,        // 14 wdm
    FileFullEaInformation,          // 15
    FileModeInformation,            // 16
    FileAlignmentInformation,       // 17
    FileAllInformation,             // 18
    FileAllocationInformation,      // 19
    FileEndOfFileInformation,       // 20 wdm
    FileAlternateNameInformation,   // 21
    FileStreamInformation,          // 22
    FilePipeInformation,            // 23
    FilePipeLocalInformation,       // 24
    FilePipeRemoteInformation,      // 25
    FileMailslotQueryInformation,   // 26
    FileMailslotSetInformation,     // 27
    FileCompressionInformation,     // 28
    FileObjectIdInformation,        // 29
    FileCompletionInformation,      // 30
    FileMoveClusterInformation,     // 31
    FileQuotaInformation,           // 32
    FileReparsePointInformation,    // 33
    FileNetworkOpenInformation,     // 34
    FileAttributeTagInformation,    // 35
    FileTrackingInformation,        // 36
    FileIdBothDirectoryInformation, // 37
    FileIdFullDirectoryInformation, // 38
    FileMaximumInformation
} FILE_INFORMATION_CLASS, *PFILE_INFORMATION_CLASS;

typedef struct _FILE_NAME_INFORMATION
{
    ULONG FileNameLength;
    WCHAR FileName[1];
} FILE_NAME_INFORMATION, *PFILE_NAME_INFORMATION;

typedef LONG NTSTATUS;

typedef struct _IO_STATUS_BLOCK 
{
    union
    {
        NTSTATUS Status;
        PVOID Pointer;
    };
    ULONG_PTR Information;
} IO_STATUS_BLOCK, *PIO_STATUS_BLOCK;

NTSYSAPI 
NTSTATUS
NTAPI
NtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, 
                       ULONG Length, FILE_INFORMATION_CLASS FileInformationClass); 



int main(int argc, char **argv)
{
    int rc = 0;
    int i;

    NTSTATUS (NTAPI *pfnNtQueryInformationFile)(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation,  
                                                ULONG Length, FILE_INFORMATION_CLASS FileInformationClass); 

    pfnNtQueryInformationFile = GetProcAddress(LoadLibrary("ntdll.dll"), "NtQueryInformationFile");

    for (i = 1; i < argc; i++)
    {
        HANDLE hFile = CreateFile(argv[i],
                                  GENERIC_READ,
                                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                  NULL,
                                  OPEN_EXISTING,
                                  FILE_FLAG_BACKUP_SEMANTICS,
                                  NULL);
        if (hFile)
        {
            long rcNt;
            char abBuf[4096];
            IO_STATUS_BLOCK Ios;

            memset(abBuf, 0, sizeof(abBuf));
            memset(&Ios, 0, sizeof(Ios));
            rcNt = pfnNtQueryInformationFile(hFile, &Ios, abBuf, sizeof(abBuf), FileNameInformation);
            if (rcNt >= 0)
            {
                PFILE_NAME_INFORMATION pFileNameInfo = (PFILE_NAME_INFORMATION)abBuf;
                printf("#%d: %s - rcNt=%#x - FileNameInformation:\n"
                       "        FileName: %ls\n"
                       "  FileNameLength: %lu\n", 
                       i, argv[i], rcNt, 
                       pFileNameInfo->FileName, 
                       pFileNameInfo->FileNameLength
                       );
            }
            else
                printf("#%d: %s - rcNt=%#x - FileNameInformation!\n", i, argv[i], rcNt);

            CloseHandle(hFile);
        }
        else
        {
            printf("#%d: %s - open failed, last error %d\n", i, argv[i], GetLastError());
            rc = 1;
        }
    }
    return rc;
}
