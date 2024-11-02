/* $Id: ntstuff.h 3644 2024-11-02 23:41:03Z knut.osmundsen@oracle.com $ */
/** @file
 * Definitions, types, prototypes and globals for NT.
 */

/*
 * Copyright (c) 2005-2013 knut st. osmundsen <bird-kBuild-spamx@anduin.net>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Alternatively, the content of this file may be used under the terms of the
 * GPL version 2 or later, or LGPL version 2.1 or later.
 */


#ifndef ___nt_ntstuff_h
#define ___nt_ntstuff_h

#define timeval timeval_Windows
#define WIN32_NO_STATUS
#include <Windows.h>
#include <winternl.h>
#undef WIN32_NO_STATUS
#include <ntstatus.h>
#undef timeval

#include <k/kTypes.h>


/** @defgroup grp_nt_ntstuff NT Stuff
 * @{ */

typedef LONG MY_NTSTATUS;
typedef ULONG MY_ACCESS_MASK;

typedef struct MY_IO_STATUS_BLOCK
{
    union
    {
        MY_NTSTATUS     Status;
        PVOID           Pointer;
    } u;
    ULONG_PTR           Information;
} MY_IO_STATUS_BLOCK;

typedef VOID WINAPI MY_IO_APC_ROUTINE(PVOID, MY_IO_STATUS_BLOCK *, ULONG);

typedef struct MY_UNICODE_STRING
{
    USHORT              Length;
    USHORT              MaximumLength;
    PWSTR               Buffer;
} MY_UNICODE_STRING;

typedef struct MY_STRING
{
    USHORT              Length;
    USHORT              MaximumLength;
    PCHAR               Buffer;
} MY_STRING;
typedef MY_STRING MY_ANSI_STRING;

typedef struct MY_CURDIR
{
    UNICODE_STRING      DosPath;
    HANDLE              Handle;
} MY_CURDIR;
typedef MY_CURDIR *PMY_CURDIR;

typedef struct MY_RTL_DRIVE_LETTER_CURDIR
{
    USHORT              Flags;
    USHORT              Length;
    ULONG               TimeStamp;
    MY_ANSI_STRING      DosPath;
} MY_RTL_DRIVE_LETTER_CURDIR;
typedef MY_RTL_DRIVE_LETTER_CURDIR *PRTL_DRIVE_LETTER_CURDIR;

typedef struct MY_RTL_USER_PROCESS_PARAMETERS
{
    ULONG               MaximumLength;
    ULONG               Length;
    ULONG               Flags;
    ULONG               DebugFlags;
    HANDLE              ConsoleHandle;
    ULONG               ConsoleFlags;
    HANDLE              StandardInput;
    HANDLE              StandardOutput;
    HANDLE              StandardError;
    MY_CURDIR           CurrentDirectory;
    MY_UNICODE_STRING   DllPath;
    MY_UNICODE_STRING   ImagePathName;
    MY_UNICODE_STRING   CommandLine;
    PWSTR               Environment;
    ULONG               StartingX;
    ULONG               StartingY;
    ULONG               CountX;
    ULONG               CountY;
    ULONG               CountCharsX;
    ULONG               CountCharsY;
    ULONG               FillAttribute;
    ULONG               WindowFlags;
    ULONG               ShowWindowFlags;
    MY_UNICODE_STRING   WindowTitle;
    MY_UNICODE_STRING   DesktopInfo;
    MY_UNICODE_STRING   ShellInfo;
    MY_UNICODE_STRING   RuntimeInfo;
    MY_RTL_DRIVE_LETTER_CURDIR CurrentDirectories[0x20];
    SIZE_T              EnvironmentSize;        /* >= Vista+ */
    SIZE_T              EnvironmentVersion;     /* >= Windows 7. */
    PVOID               PackageDependencyData;  /* >= Windows 8 or Windows 8.1. */
    ULONG               ProcessGroupId;         /* >= Windows 8 or Windows 8.1. */
} MY_RTL_USER_PROCESS_PARAMETERS;
typedef MY_RTL_USER_PROCESS_PARAMETERS *PMY_RTL_USER_PROCESS_PARAMETERS;

typedef struct MY_OBJECT_ATTRIBUTES
{
    ULONG               Length;
    HANDLE              RootDirectory;
    MY_UNICODE_STRING  *ObjectName;
    ULONG               Attributes;
    PVOID               SecurityDescriptor;
    PVOID               SecurityQualityOfService;
} MY_OBJECT_ATTRIBUTES;

#define MyInitializeObjectAttributes(a_pAttr, a_pName, a_fAttribs, a_hRoot, a_pSecDesc) \
    do { \
        (a_pAttr)->Length                   = sizeof(MY_OBJECT_ATTRIBUTES); \
        (a_pAttr)->RootDirectory            = (a_hRoot); \
        (a_pAttr)->Attributes               = (a_fAttribs); \
        (a_pAttr)->ObjectName               = (a_pName); \
        (a_pAttr)->SecurityDescriptor       = (a_pSecDesc); \
        (a_pAttr)->SecurityQualityOfService = NULL; \
    } while (0)



typedef struct MY_FILE_BASIC_INFORMATION
{
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    ULONG           FileAttributes;
} MY_FILE_BASIC_INFORMATION;

typedef struct MY_FILE_STANDARD_INFORMATION
{
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           NumberOfLinks;
    BOOLEAN         DeletePending;
    BOOLEAN         Directory;
} MY_FILE_STANDARD_INFORMATION;

typedef struct MY_FILE_NETWORK_OPEN_INFORMATION
{
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   AllocationSize;
    LARGE_INTEGER   EndOfFile;
    ULONG           FileAttributes;
    ULONG           AlignmentPadding;
} MY_FILE_NETWORK_OPEN_INFORMATION;

typedef struct MY_FILE_INTERNAL_INFORMATION
{
    LARGE_INTEGER   IndexNumber;
} MY_FILE_INTERNAL_INFORMATION;

typedef struct MY_FILE_EA_INFORMATION
{
    ULONG           EaSize;
} MY_FILE_EA_INFORMATION;

typedef struct MY_FILE_ACCESS_INFORMATION
{
    ACCESS_MASK     AccessFlags;
} MY_FILE_ACCESS_INFORMATION;

typedef struct MY_FILE_POSITION_INFORMATION
{
    LARGE_INTEGER   CurrentByteOffset;
} MY_FILE_POSITION_INFORMATION;

typedef struct MY_FILE_MODE_INFORMATION
{
    ULONG           Mode;
} MY_FILE_MODE_INFORMATION;

typedef struct MY_FILE_ALIGNMENT_INFORMATION
{
    ULONG           AlignmentRequirement;
} MY_FILE_ALIGNMENT_INFORMATION;

typedef struct MY_FILE_NAME_INFORMATION
{
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} MY_FILE_NAME_INFORMATION;

typedef struct MY_FILE_ALL_INFORMATION
{
    MY_FILE_BASIC_INFORMATION       BasicInformation;
    MY_FILE_STANDARD_INFORMATION    StandardInformation;
    MY_FILE_INTERNAL_INFORMATION    InternalInformation;
    MY_FILE_EA_INFORMATION          EaInformation;
    MY_FILE_ACCESS_INFORMATION      AccessInformation;
    MY_FILE_POSITION_INFORMATION    PositionInformation;
    MY_FILE_MODE_INFORMATION        ModeInformation;
    MY_FILE_ALIGNMENT_INFORMATION   AlignmentInformation;
    MY_FILE_NAME_INFORMATION        NameInformation;
} MY_FILE_ALL_INFORMATION;

typedef struct MY_FILE_ATTRIBUTE_TAG_INFORMATION
{
    ULONG           FileAttributes;
    ULONG           ReparseTag;
} MY_FILE_ATTRIBUTE_TAG_INFORMATION;


typedef struct MY_FILE_NAMES_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    ULONG           FileNameLength;
    WCHAR           FileName[1];
} MY_FILE_NAMES_INFORMATION;
/** The sizeof(MY_FILE_NAMES_INFORMATION) without the FileName. */
#define MIN_SIZEOF_MY_FILE_NAMES_INFORMATION  (4 + 4 + 4)


typedef struct MY_FILE_ID_FULL_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    LARGE_INTEGER   FileId;
    WCHAR           FileName[1];
} MY_FILE_ID_FULL_DIR_INFORMATION;
/** The sizeof(MY_FILE_NAMES_INFORMATION) without the FileName. */
#define MIN_SIZEOF_MY_FILE_ID_FULL_DIR_INFORMATION  ( (size_t)&((MY_FILE_ID_FULL_DIR_INFORMATION *)0)->FileName )

typedef struct MY_FILE_BOTH_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    CCHAR           ShortNameLength;
    WCHAR           ShortName[12];
    WCHAR           FileName[1];
} MY_FILE_BOTH_DIR_INFORMATION;
/** The sizeof(MY_FILE_BOTH_DIR_INFORMATION) without the FileName. */
#define MIN_SIZEOF_MY_FILE_BOTH_DIR_INFORMATION  ( (size_t)&((MY_FILE_BOTH_DIR_INFORMATION *)0)->FileName )


typedef struct MY_FILE_ID_BOTH_DIR_INFORMATION
{
    ULONG           NextEntryOffset;
    ULONG           FileIndex;
    LARGE_INTEGER   CreationTime;
    LARGE_INTEGER   LastAccessTime;
    LARGE_INTEGER   LastWriteTime;
    LARGE_INTEGER   ChangeTime;
    LARGE_INTEGER   EndOfFile;
    LARGE_INTEGER   AllocationSize;
    ULONG           FileAttributes;
    ULONG           FileNameLength;
    ULONG           EaSize;
    CCHAR           ShortNameLength;
    WCHAR           ShortName[12];
    LARGE_INTEGER   FileId;
    WCHAR           FileName[1];
} MY_FILE_ID_BOTH_DIR_INFORMATION;
/** The sizeof(MY_FILE_NAMES_INFORMATION) without the FileName. */
#define MIN_SIZEOF_MY_FILE_ID_BOTH_DIR_INFORMATION  ( (size_t)&((MY_FILE_ID_BOTH_DIR_INFORMATION *)0)->FileName )


typedef struct MY_FILE_DISPOSITION_INFORMATION
{
    BOOLEAN         DeleteFile;
} MY_FILE_DISPOSITION_INFORMATION;


typedef enum MY_FILE_INFORMATION_CLASS
{
    MyFileDirectoryInformation                     = 1,
    MyFileFullDirectoryInformation,             /* = 2  */
    MyFileBothDirectoryInformation,             /* = 3  */
    MyFileBasicInformation,                     /* = 4  */
    MyFileStandardInformation,                  /* = 5  */
    MyFileInternalInformation,                  /* = 6  */
    MyFileEaInformation,                        /* = 7  */
    MyFileAccessInformation,                    /* = 8  */
    MyFileNameInformation,                      /* = 9  */
    MyFileRenameInformation,                    /* = 10 */
    MyFileLinkInformation,                      /* = 11 */
    MyFileNamesInformation,                     /* = 12 */
    MyFileDispositionInformation,               /* = 13 */
    MyFilePositionInformation,                  /* = 14 */
    MyFileFullEaInformation,                    /* = 15 */
    MyFileModeInformation,                      /* = 16 */
    MyFileAlignmentInformation,                 /* = 17 */
    MyFileAllInformation,                       /* = 18 */
    MyFileAllocationInformation,                /* = 19 */
    MyFileEndOfFileInformation,                 /* = 20 */
    MyFileAlternateNameInformation,             /* = 21 */
    MyFileStreamInformation,                    /* = 22 */
    MyFilePipeInformation,                      /* = 23 */
    MyFilePipeLocalInformation,                 /* = 24 */
    MyFilePipeRemoteInformation,                /* = 25 */
    MyFileMailslotQueryInformation,             /* = 26 */
    MyFileMailslotSetInformation,               /* = 27 */
    MyFileCompressionInformation,               /* = 28 */
    MyFileObjectIdInformation,                  /* = 29 */
    MyFileCompletionInformation,                /* = 30 */
    MyFileMoveClusterInformation,               /* = 31 */
    MyFileQuotaInformation,                     /* = 32 */
    MyFileReparsePointInformation,              /* = 33 */
    MyFileNetworkOpenInformation,               /* = 34 */
    MyFileAttributeTagInformation,              /* = 35 */
    MyFileTrackingInformation,                  /* = 36 */
    MyFileIdBothDirectoryInformation,           /* = 37 */
    MyFileIdFullDirectoryInformation,           /* = 38 */
    MyFileValidDataLengthInformation,           /* = 39 */
    MyFileShortNameInformation,                 /* = 40 */
    MyFileIoCompletionNotificationInformation,  /* = 41 */
    MyFileIoStatusBlockRangeInformation,        /* = 42 */
    MyFileIoPriorityHintInformation,            /* = 43 */
    MyFileSfioReserveInformation,               /* = 44 */
    MyFileSfioVolumeInformation,                /* = 45 */
    MyFileHardLinkInformation,                  /* = 46 */
    MyFileProcessIdsUsingFileInformation,       /* = 47 */
    MyFileNormalizedNameInformation,            /* = 48 */
    MyFileNetworkPhysicalNameInformation,       /* = 49 */
    MyFileIdGlobalTxDirectoryInformation,       /* = 50 */
    MyFileIsRemoteDeviceInformation,            /* = 51 */
    MyFileAttributeCacheInformation,            /* = 52 */
    MyFileNumaNodeInformation,                  /* = 53 */
    MyFileStandardLinkInformation,              /* = 54 */
    MyFileRemoteProtocolInformation,            /* = 55 */
    MyFileMaximumInformation
} MY_FILE_INFORMATION_CLASS;


typedef struct MY_FILE_FS_VOLUME_INFORMATION
{
    LARGE_INTEGER   VolumeCreationTime;
    ULONG           VolumeSerialNumber;
    ULONG           VolumeLabelLength;
    BOOLEAN         SupportsObjects;
    WCHAR           VolumeLabel[1];
} MY_FILE_FS_VOLUME_INFORMATION;

typedef struct _MY_FILE_FS_ATTRIBUTE_INFORMATION
{
    ULONG           FileSystemAttributes;
    LONG            MaximumComponentNameLength;
    ULONG           FileSystemNameLength;
    WCHAR           FileSystemName[1];
} MY_FILE_FS_ATTRIBUTE_INFORMATION;

typedef enum MY_FSINFOCLASS
{
    MyFileFsVolumeInformation                      = 1,
    MyFileFsLabelInformation,                   /* = 2  */
    MyFileFsSizeInformation,                    /* = 3  */
    MyFileFsDeviceInformation,                  /* = 4  */
    MyFileFsAttributeInformation,               /* = 5  */
    MyFileFsControlInformation,                 /* = 6  */
    MyFileFsFullSizeInformation,                /* = 7  */
    MyFileFsObjectIdInformation,                /* = 8  */
    MyFileFsDriverPathInformation,              /* = 9  */
    MyFileFsVolumeFlagsInformation,             /* = 10 */
    MyFileFsMaximumInformation
} MY_FS_INFORMATION_CLASS;


typedef struct MY_RTLP_CURDIR_REF
{
    LONG            RefCount;
    HANDLE          Handle;
} MY_RTLP_CURDIR_REF;

typedef struct MY_RTL_RELATIVE_NAME_U
{
    MY_UNICODE_STRING   RelativeName;
    HANDLE              ContainingDirectory;
    MY_RTLP_CURDIR_REF  CurDirRef;
} MY_RTL_RELATIVE_NAME_U;


#ifndef OBJ_INHERIT
# define OBJ_INHERIT                        0x00000002U
# define OBJ_PERMANENT                      0x00000010U
# define OBJ_EXCLUSIVE                      0x00000020U
# define OBJ_CASE_INSENSITIVE               0x00000040U
# define OBJ_OPENIF                         0x00000080U
# define OBJ_OPENLINK                       0x00000100U
# define OBJ_KERNEL_HANDLE                  0x00000200U
# define OBJ_FORCE_ACCESS_CHECK             0x00000400U
# define OBJ_VALID_ATTRIBUTES               0x000007f2U
#endif

#ifndef FILE_OPEN
# define FILE_SUPERSEDE                     0x00000000U
# define FILE_OPEN                          0x00000001U
# define FILE_CREATE                        0x00000002U
# define FILE_OPEN_IF                       0x00000003U
# define FILE_OVERWRITE                     0x00000004U
# define FILE_OVERWRITE_IF                  0x00000005U
# define FILE_MAXIMUM_DISPOSITION           0x00000005U
#endif

#ifndef FILE_DIRECTORY_FILE
# define FILE_DIRECTORY_FILE                0x00000001U
# define FILE_WRITE_THROUGH                 0x00000002U
# define FILE_SEQUENTIAL_ONLY               0x00000004U
# define FILE_NO_INTERMEDIATE_BUFFERING     0x00000008U
# define FILE_SYNCHRONOUS_IO_ALERT          0x00000010U
# define FILE_SYNCHRONOUS_IO_NONALERT       0x00000020U
# define FILE_NON_DIRECTORY_FILE            0x00000040U
# define FILE_CREATE_TREE_CONNECTION        0x00000080U
# define FILE_COMPLETE_IF_OPLOCKED          0x00000100U
# define FILE_NO_EA_KNOWLEDGE               0x00000200U
# define FILE_OPEN_REMOTE_INSTANCE          0x00000400U
# define FILE_RANDOM_ACCESS                 0x00000800U
# define FILE_DELETE_ON_CLOSE               0x00001000U
# define FILE_OPEN_BY_FILE_ID               0x00002000U
# define FILE_OPEN_FOR_BACKUP_INTENT        0x00004000U
# define FILE_NO_COMPRESSION                0x00008000U
# define FILE_RESERVE_OPFILTER              0x00100000U
# define FILE_OPEN_REPARSE_POINT            0x00200000U
# define FILE_OPEN_NO_RECALL                0x00400000U
# define FILE_OPEN_FOR_FREE_SPACE_QUERY     0x00800000U
#endif

#ifndef DUPLICATE_CLOSE_SOURCE /* For the misnomer NtDuplicateObject. */
# define DUPLICATE_CLOSE_SOURCE             0x00000001U
# define DUPLICATE_SAME_ACCESS              0x00000002U
#endif
#ifndef DUPLICATE_SAME_ATTRIBUTES
# define DUPLICATE_SAME_ATTRIBUTES          0x00000004U
#endif


/** @name NT status codes and associated macros.
 * @{ */
#define MY_NT_SUCCESS(a_ntRc)               ((MY_NTSTATUS)(a_ntRc) >= 0)
#define MY_NT_FAILURE(a_ntRc)               ((MY_NTSTATUS)(a_ntRc) <  0)
#define MY_STATUS_NO_MORE_FILES             ((MY_NTSTATUS)0x80000006)
#define MY_STATUS_OBJECT_NAME_INVALID       ((MY_NTSTATUS)0xc0000033)
#define MY_STATUS_OBJECT_NAME_NOT_FOUND     ((MY_NTSTATUS)0xc0000034)
#define MY_STATUS_OBJECT_PATH_INVALID       ((MY_NTSTATUS)0xc0000039)
#define MY_STATUS_OBJECT_PATH_NOT_FOUND     ((MY_NTSTATUS)0xc000003a)
#define MY_STATUS_OBJECT_PATH_SYNTAX_BAD    ((MY_NTSTATUS)0xc000003b)
/** @}  */

/** The pseudohandle for the current process. */
#define MY_NT_CURRENT_PROCESS               ((HANDLE)~(uintptr_t)0)
/** The pseudohandle for the current thread. */
#define MY_NT_CURRENT_THREAD                ((HANDLE)~(uintptr_t)1)

typedef struct MY_CLIENT_ID
{
    HANDLE UniqueProcess;
    HANDLE UniqueThread;
} MY_CLIENT_ID;

/** Partial TEB.   */
typedef struct MY_PARTIAL_TEB
{
    NT_TIB          NtTib;
    PVOID           EnvironmentPointer;
    MY_CLIENT_ID    ClientId;
    PVOID           ActiveRpcHandle;
    PVOID           ThreadLocalStoragePointer;
    PPEB            ProcessEnvironmentBlock;
    KU32            LastErrorValue;
    KU32            CountOfOwnedCriticalSections;
    PVOID           CsrClientThread;
    PVOID           Win32ThreadInfo;
} MY_PARTIAL_TEB;

/** Internal macro for reading uintptr_t sized TEB members. */
#if K_ARCH == K_ARCH_AMD64
# define MY_NT_READ_TEB_WORKER(a_offTebMember) ( __readgsqword(a_offTebMember) )
#elif K_ARCH == K_ARCH_X86_32
# define MY_NT_READ_TEB_WORKER(a_offTebMember) ( __readfsdword(a_offTebMember) )
#elif K_ARCH == K_ARCH_ARM_64
# define MY_NT_READ_TEB_WORKER(a_offTebMember) ( __readx18qword(a_offTebMember) )
#else
# error "Port me!"
#endif
/** Get the PEB pointer.
 * @remark Needs stddef.h. */
#define MY_NT_CURRENT_PEB()  ( (PPEB)MY_NT_READ_TEB_WORKER(offsetof(MY_PARTIAL_TEB, ProcessEnvironmentBlock)) )
/** Get the TEB pointer.
 * @remark Needs stddef.h. */
#define MY_NT_CURRENT_TEB()  ( (PTEB)MY_NT_READ_TEB_WORKER(offsetof(NT_TIB, Self)) )


/*******************************************************************************
*   Global Variables                                                           *
*******************************************************************************/
extern MY_NTSTATUS (WINAPI * g_pfnNtClose)(HANDLE);
extern MY_NTSTATUS (WINAPI * g_pfnNtCreateFile)(PHANDLE, MY_ACCESS_MASK, MY_OBJECT_ATTRIBUTES *, MY_IO_STATUS_BLOCK *,
                                                PLARGE_INTEGER, ULONG, ULONG, ULONG, ULONG, PVOID, ULONG);
extern MY_NTSTATUS (WINAPI * g_pfnNtDeleteFile)(MY_OBJECT_ATTRIBUTES *);
extern MY_NTSTATUS (WINAPI * g_pfnNtDuplicateObject)(HANDLE hSrcProc, HANDLE hSrc, HANDLE hDstProc, HANDLE *phRet,
                                                     MY_ACCESS_MASK fDesiredAccess, ULONG fAttribs, ULONG fOptions);
extern MY_NTSTATUS (WINAPI * g_pfnNtReadFile)(HANDLE hFile, HANDLE hEvent, MY_IO_APC_ROUTINE *pfnApc, PVOID pvApcCtx,
                                              MY_IO_STATUS_BLOCK *, PVOID pvBuf, ULONG cbToRead, PLARGE_INTEGER poffFile,
                                              PULONG  puKey);
extern MY_NTSTATUS (WINAPI * g_pfnNtQueryInformationFile)(HANDLE, MY_IO_STATUS_BLOCK *,
                                                          PVOID, LONG, MY_FILE_INFORMATION_CLASS);
extern MY_NTSTATUS (WINAPI * g_pfnNtQueryVolumeInformationFile)(HANDLE, MY_IO_STATUS_BLOCK *,
                                                                PVOID, LONG, MY_FS_INFORMATION_CLASS);
extern MY_NTSTATUS (WINAPI * g_pfnNtQueryDirectoryFile)(HANDLE, HANDLE, MY_IO_APC_ROUTINE *, PVOID, MY_IO_STATUS_BLOCK *,
                                                        PVOID, ULONG, MY_FILE_INFORMATION_CLASS, BOOLEAN,
                                                        MY_UNICODE_STRING *, BOOLEAN);
extern MY_NTSTATUS (WINAPI * g_pfnNtQueryAttributesFile)(MY_OBJECT_ATTRIBUTES *, MY_FILE_BASIC_INFORMATION *);
extern MY_NTSTATUS (WINAPI * g_pfnNtQueryFullAttributesFile)(MY_OBJECT_ATTRIBUTES *, MY_FILE_NETWORK_OPEN_INFORMATION *);
extern MY_NTSTATUS (WINAPI * g_pfnNtSetInformationFile)(HANDLE, MY_IO_STATUS_BLOCK *, PVOID, LONG, MY_FILE_INFORMATION_CLASS);
extern BOOLEAN     (WINAPI * g_pfnRtlDosPathNameToNtPathName_U)(PCWSTR, MY_UNICODE_STRING *, PCWSTR *, MY_RTL_RELATIVE_NAME_U *);
extern MY_NTSTATUS (WINAPI * g_pfnRtlAnsiStringToUnicodeString)(MY_UNICODE_STRING *, MY_ANSI_STRING const *, BOOLEAN);
extern MY_NTSTATUS (WINAPI * g_pfnRtlUnicodeStringToAnsiString)(MY_ANSI_STRING *, MY_UNICODE_STRING *, BOOLEAN);
extern BOOLEAN     (WINAPI * g_pfnRtlEqualUnicodeString)(MY_UNICODE_STRING const *pUniStr1, MY_UNICODE_STRING const *pUniStr2,
                                                         BOOLEAN fCaseInsensitive);
extern BOOLEAN     (WINAPI * g_pfnRtlEqualString)(MY_ANSI_STRING const *pAnsiStr1, MY_ANSI_STRING const *pAnsiStr2,
                                                  BOOLEAN fCaseInsensitive);
extern UCHAR       (WINAPI * g_pfnRtlUpperChar)(UCHAR uch);
extern ULONG       (WINAPI * g_pfnRtlNtStatusToDosError)(MY_NTSTATUS rcNt);
extern VOID        (WINAPI * g_pfnRtlAcquirePebLock)(VOID);
extern VOID        (WINAPI * g_pfnRtlReleasePebLock)(VOID);


/** @} */

#endif

