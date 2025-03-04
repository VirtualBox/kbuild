/* Output to stdout / stderr for GNU make
Copyright (C) 2013-2016 Free Software Foundation, Inc.
This file is part of GNU Make.

GNU Make is free software; you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation; either version 3 of the License, or (at your option) any later
version.

GNU Make is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR
A PARTICULAR PURPOSE.  See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.  */

#if defined (KBUILD_OS_WINDOWS) && (defined(KMK) || defined(DEBUG_STDOUT_CLOSE_ISSUE))
# include "nt/ntstuff.h"
# include "nt/nthlp.h"
# include <process.h>
#endif
#include "makeint.h"
#include "job.h"

/* GNU make no longer supports pre-ANSI89 environments.  */

#include <assert.h>
#include <stdio.h>
#include <stdarg.h>

#ifdef HAVE_UNISTD_H
# include <unistd.h>
#endif

#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#else
# include <sys/file.h>
#endif

#ifdef WINDOWS32
# include <windows.h>
# include <io.h>
# ifndef CONFIG_NEW_WIN_CHILDREN
#  include "sub_proc.h"
# else
#  include "w32/winchildren.h"
# endif
#endif /* WINDOWS32 */
#ifdef KBUILD_OS_WINDOWS
# include "console.h"
#endif

struct output *output_context = NULL;
unsigned int stdio_traced = 0;

#define OUTPUT_NONE (-1)

#define OUTPUT_ISSET(_out) ((_out)->out >= 0 || (_out)->err >= 0)

#ifdef HAVE_FCNTL_H
# define STREAM_OK(_s) ((fcntl (fileno (_s), F_GETFD) != -1) || (errno != EBADF))
#else
# define STREAM_OK(_s) 1
#endif

#ifdef DEBUG_STDOUT_CLOSE_ISSUE

/* fflush wrapper w/ error checking + reporting for stdout.
   This is to debug the mysterious 'kmk: write error: stdout' errors. */
int g_fStdOutError = 0;

# ifdef KBUILD_OS_WINDOWS
static void my_output_pipe_info (HANDLE hStdOut, const char *pszWhere)
{
  MY_IO_STATUS_BLOCK Ios = {0} ;
  struct MY_FILE_PIPE_INFORMATION
  {
      ULONG           ReadMode;
      ULONG           CompletionMode;
  } Info1 = {0};
  MY_NTSTATUS rcNt1 = g_pfnNtQueryInformationFile(hStdOut, &Ios, &Info1, sizeof(Info1), MyFilePipeInformation);
  struct MY_FILE_PIPE_LOCAL_INFORMATION
  {
      ULONG           NamedPipeType;
      ULONG           NamedPipeConfiguration;
      ULONG           MaximumInstances;
      ULONG           CurrentInstances;
      ULONG           InboundQuota;
      ULONG           ReadDataAvailable;
      ULONG           OutboundQuota;
      ULONG           WriteQuotaAvailable;
      ULONG           NamedPipeState;
      ULONG           NamedPipeEnd;
  } Info2 = {0};
  MY_NTSTATUS rcNt2 = g_pfnNtQueryInformationFile(hStdOut, &Ios, &Info2, sizeof(Info2), MyFilePipeLocalInformation);
  union { BYTE ab[1024]; MY_FILE_NAME_INFORMATION NameInfo; } uBuf = {{0}};
  MY_NTSTATUS rcNt3 = g_pfnNtQueryInformationFile(hStdOut, &Ios, &uBuf, sizeof(uBuf) - sizeof(WCHAR), MyFileNameInformation);
  DWORD dwMode = 0;
  MY_NTSTATUS rcNt4 = g_pfnNtQueryInformationFile(hStdOut, &Ios, &dwMode, sizeof(dwMode), MyFileModeInformation);
  fprintf(stderr, "kmk[%u/%u]: stdout pipeinfo at %s: fmode=%#x pipemode=%#x complmode=%#x type=%#x cfg=%#x instances=%u/%u inquota=%#x readable=%#x outquota=%#x writable=%#x state=%#x end=%#x hStdOut=%p %S rcNt=%#x/%#x/%#x/%#x\n",
          makelevel, _getpid(), pszWhere, dwMode, Info1.ReadMode, Info1.CompletionMode, Info2.NamedPipeType,
          Info2.NamedPipeConfiguration, Info2.CurrentInstances, Info2.MaximumInstances, Info2.InboundQuota,
          Info2.ReadDataAvailable, Info2.OutboundQuota, Info2.WriteQuotaAvailable, Info2.NamedPipeState, Info2.NamedPipeEnd,
          hStdOut, uBuf.NameInfo.FileName, rcNt1, rcNt2, rcNt3, rcNt4);
}
# endif /* KBUILD_OS_WINDOWS */

static void my_stdout_error (const char *pszOperation, const char *pszMessage)
{
# ifdef KBUILD_OS_WINDOWS
  DWORD const     dwErr   = GetLastError ();
# endif
  int const       iErrNo  = errno;
  int const       fdFile  = fileno (stdout);
# ifdef KBUILD_OS_WINDOWS
  HANDLE    const hNative = (HANDLE)_get_osfhandle (_fileno (stdout));
  DWORD const     dwType  = GetFileType (hNative);
  unsigned int    uDosErr = _doserrno;
  if ((dwType & ~FILE_TYPE_REMOTE) == FILE_TYPE_PIPE)
    my_output_pipe_info (hNative, "error");
  fprintf (stderr, "kmk[%u/%u]: stdout error: %s: %s! (lasterr=%u errno=%d fileno=%d uDosErr=%u native=%p type=%#x)\n",
           makelevel, _getpid(), pszOperation, pszMessage, dwErr, iErrNo, fdFile, uDosErr, hNative, dwType);
# else
  fprintf (stderr, "kmk[%u]: stdout error: %s: %s! (lasterr=%u errno=%d fileno=%d)\n",
           makelevel, pszOperation, pszMessage, dwErr, iErrNo, fdFile);
# endif
}

/* Preserves errno and win32 last error. */
void my_check_stdout (const char *pszWhere)
{
  if (!g_fStdOutError)
    {
# ifdef KBUILD_OS_WINDOWS
      DWORD const dwErrSaved  = GetLastError();
# endif
      int const   iErrNoSaved = errno;

      if (ferror (stdout))
        {
          my_stdout_error (pszWhere, "error pending!");
          g_fStdOutError = 1;
        }

      errno = iErrNoSaved;
# ifdef KBUILD_OS_WINDOWS
      SetLastError(dwErrSaved);
# endif
    }
}

# ifdef KBUILD_OS_WINDOWS
/* generic fwrite wrapper */
__declspec(dllexport) size_t __cdecl fwrite(void const *pvBuf, size_t cbItem, size_t cItems, FILE *pFile)
{
  size_t cbWritten;
  if (pFile == stdout)
    my_check_stdout("fwrite/entry");
  _lock_file(pFile);
  cbWritten = _fwrite_nolock(pvBuf, cbItem, cItems, pFile);
  _unlock_file(pFile);
  if (pFile == stdout)
    my_check_stdout("fwrite/exit");
  return cbWritten;
}
void * const __imp_fwrite  = (void *)(uintptr_t)fwrite;

/* generic fflush wrapper */
__declspec(dllexport) int __cdecl fflush(FILE *pFile)
{
  int rc;
  if (pFile == stdout || !pFile)
    my_check_stdout("fflush/entry");
  if (pFile)
    {
      _lock_file(pFile);
      rc = _fflush_nolock(pFile);
      _unlock_file(pFile);
    }
  if (pFile == stdout || !pFile)
    my_check_stdout("fflush/exit");
  return rc;
}
void * const __imp_fflush  = (void *)(uintptr_t)fflush;

# else
static int my_fflush (FILE *pFile)
{
  if (pFile == stdout && !g_fStdOutError)
    {
      if (!ferror (pFile))
        {
          int rcRet = fflush (pFile);
          g_fStdOutError = ferror (pFile);
          if (rcRet != EOF && !g_fStdOutError)
            { /* likely */ }
          else if (rcRet == EOF)
            my_stdout_error ("fflush(stdout)", "flush failed!");
          else
            my_stdout_error ("fflush(stdout)", "error pending after successful flush!");

          return rcRet;
        }
      else
        {
          my_stdout_error ("fflush(stdout)", "error pending on entry!");
          g_fStdOutError = 1;
        }

    }
  return fflush (pFile);
}

#  undef  fflush
#  define fflush(a_pFile) my_fflush(a_pFile)
# endif

#endif /* DEBUG_STDOUT_CLOSE_ISSUE */

#if defined(KBUILD_OS_WINDOWS) && defined(KMK)
/*
   Windows Asynchronous (Overlapping) Pipe Hack
   --------------------------------------------

   If a write pipe is opened with FILE_FLAG_OVERLAPPED or equivalent flags,
   concurrent WriteFile calls on that pipe may run into a race causing the
   wrong thread to be worken up on completion.  Since the write is still
   pending, the number of bytes written hasn't been set and is still zero.
   This leads to UCRT setting errno = ENOSPC and may cause stack corruption
   when the write is finally completed and the IO_STATUS_BLOCK is written
   by the kernel.

   To work around this problem, we detect asynchronous pipes attached to
   stdout and stderr and replaces them with standard pipes and threads
   pumping output.  The thread deals properly with the async writes. */
 
/* Data for the pipe workaround hacks. */
struct win_pipe_hacks
{
  /* 1 (stdout) or 2 (stderr). */
  int           fd;
  int volatile  fShutdown;
  /** Event handle for overlapped I/O. */
  HANDLE        hEvt;
  /* The original pipe that's in overlapping state (write end). */
  HANDLE        hDstPipe;
  /* The replacement pipe (read end). */
  HANDLE        hSrcPipe;
  /** The thread pumping bytes between the two pipes. */
  HANDLE        hThread;
  /** Putting the overlapped I/O structure here is safer.   */
  OVERLAPPED    Overlapped;
} g_pipe_workarounds[2] =
{
  { 1, 0, NULL, NULL, NULL, NULL, { 0, 0, {{ 0, 0}}} },
  { 2, 0, NULL, NULL, NULL, NULL, { 0, 0, {{ 0, 0}}} },
};

/* Thread function that pumps bytes between our pipe and the parents pipe. */
static unsigned __stdcall win_pipe_pump_thread (void *user)
{
  unsigned const idx   = (unsigned)(intptr_t)user;
  int            fQuit = 0;
  do
    {
      /* Read from the source pipe (our). */
      char  achBuf[4096];
      DWORD cbRead = 0;
      if (ReadFile (g_pipe_workarounds[idx].hSrcPipe, achBuf, sizeof(achBuf), &cbRead, NULL))
        {
          for (unsigned iWrite = 0, off = 0; off < cbRead && !fQuit; iWrite++)
            {
              /* Write the data we've read to the origianl pipe, using overlapped
                 I/O.  This should work fine even if hDstPipe wasn't opened in
                 overlapped I/O mode. */
              g_pipe_workarounds[idx].Overlapped.Internal     = 0;
              g_pipe_workarounds[idx].Overlapped.InternalHigh = 0;
              g_pipe_workarounds[idx].Overlapped.Offset       = 0xffffffff /*FILE_WRITE_TO_END_OF_FILE*/;
              g_pipe_workarounds[idx].Overlapped.OffsetHigh   = (DWORD)-1;
              g_pipe_workarounds[idx].Overlapped.hEvent       = g_pipe_workarounds[idx].hEvt;
              DWORD cbWritten = 0;
              if (!WriteFile (g_pipe_workarounds[idx].hDstPipe, &achBuf[off], cbRead - off,
                              &cbWritten, &g_pipe_workarounds[idx].Overlapped))
                {
                  if ((fQuit = GetLastError () != ERROR_IO_PENDING))
                    break;
                  if ((fQuit = !GetOverlappedResult (g_pipe_workarounds[idx].hDstPipe, &g_pipe_workarounds[idx].Overlapped,
                                                     &cbWritten, TRUE)))
                    break;
                }
              off += cbWritten;
              if (cbWritten == 0 && iWrite > 15)
                {
                  DWORD fState = 0;
                  if (   GetNamedPipeHandleState(g_pipe_workarounds[idx].hDstPipe, &fState, NULL, NULL, NULL, NULL,  0)
                      && (fState & (PIPE_WAIT | PIPE_NOWAIT)) == PIPE_NOWAIT)
                    {
                      fState &= ~PIPE_NOWAIT;
                      fState |= PIPE_WAIT;
                      if (   SetNamedPipeHandleState(g_pipe_workarounds[idx].hDstPipe, &fState, NULL, NULL)
                          && iWrite == 16)
                          continue;
                    }
                  Sleep(iWrite & 15);
                }
          }
      }
      else
        break;
    }
  while (!g_pipe_workarounds[idx].fShutdown && !fQuit);

  /* Cleanup. */
  CloseHandle (g_pipe_workarounds[idx].hSrcPipe);
  g_pipe_workarounds[idx].hSrcPipe = NULL;

  CloseHandle (g_pipe_workarounds[idx].hDstPipe);
  g_pipe_workarounds[idx].hDstPipe = NULL;

  CloseHandle (g_pipe_workarounds[idx].hEvt);
  g_pipe_workarounds[idx].hEvt = NULL;
  return 0;
}

/* Shuts down the thread pumping bytes between our pipe and the parents pipe. */
static void win_pipe_hack_terminate (void)
{
  for (unsigned idx = 0; idx < 2; idx++)
    if (g_pipe_workarounds[idx].hThread != NULL)
      {
        g_pipe_workarounds[idx].fShutdown++;
        if (g_pipe_workarounds[idx].hSrcPipe != NULL)
          CancelIoEx (g_pipe_workarounds[idx].hSrcPipe, NULL);
      }

  for (unsigned idx = 0; idx < 2; idx++)
    if (g_pipe_workarounds[idx].hThread != NULL)
      for (unsigned msWait = 64; msWait <= 1000; msWait *= 2) /* wait almost 2 seconds. */
        {
          if (g_pipe_workarounds[idx].hSrcPipe != NULL)
            CancelIoEx (g_pipe_workarounds[idx].hSrcPipe, NULL);
          DWORD dwWait = WaitForSingleObject (g_pipe_workarounds[idx].hThread, msWait);
          if (dwWait == WAIT_OBJECT_0)
            {
              CloseHandle (g_pipe_workarounds[idx].hThread);
              g_pipe_workarounds[idx].hThread = NULL;
              break;
            }
        }
}

/* Applies the asynchronous pipe hack to a standard handle.
   The hPipe argument is the handle, and idx is 0 for stdout and 1 for stderr. */
static void win_pipe_hack_apply (HANDLE hPipe, int idx, int fSameObj)
{
  /* Create a normal pipe and assign it to an CRT file descriptor. The handles
     will be created as not inheritable, but the _dup2 call below will duplicate
     the write handle with inhertiance enabled. */
  HANDLE hPipeR = NULL;
  HANDLE hPipeW = NULL;
  if (CreatePipe (&hPipeR, &hPipeW, NULL, 0x1000))
    {
      int fdTmp = _open_osfhandle ((intptr_t)hPipeW, _O_TEXT);
      if (fdTmp >= 0)
        {
          int const fOldMode = _setmode (idx + 1, _O_TEXT);
          if (fOldMode != _O_TEXT && fOldMode != -1)
            {
              _setmode (idx + 1, fOldMode);
              _setmode (fdTmp, fOldMode);
            }

          /* Create the event sempahore. */
          HANDLE hEvt = CreateEventW (NULL, FALSE, FALSE, NULL);
          if (hEvt != NULL && hEvt != INVALID_HANDLE_VALUE)
            {
              /* Duplicate the pipe, as the _dup2 call below will (probably) close it. */
              HANDLE hDstPipe = NULL;
              if (DuplicateHandle (GetCurrentProcess (), hPipe,
                                   GetCurrentProcess (), &hDstPipe,
                                   0, FALSE, DUPLICATE_SAME_ACCESS))
                {
                  /* Create a thread for safely pumping bytes between the pipes. */
                  g_pipe_workarounds[idx].hEvt      = hEvt;
                  g_pipe_workarounds[idx].fShutdown = 0;
                  g_pipe_workarounds[idx].hDstPipe  = hDstPipe;
                  g_pipe_workarounds[idx].hSrcPipe  = hPipeR;
                  HANDLE hThread = (HANDLE)_beginthreadex(NULL, 0, win_pipe_pump_thread, (void *)(intptr_t)idx, 0, NULL);
                  if (hThread != NULL && hThread != INVALID_HANDLE_VALUE)
                    {
                      g_pipe_workarounds[idx].hThread = hThread;

                      /* Now that the thread is operating, replace the file descriptors(s).
                         This involves DuplicateHandle and will call SetStdHandle. */
                      if (_dup2 (fdTmp, idx + 1) == 0)
                        {
                          if (   fSameObj
                              && _dup2 (fdTmp, idx + 2) != 0)
                            {
                              fprintf (stderr, "%s: warning: _dup2(%d,%d) failed - fSameObj=1: %s (%d, %u)",
                                       program, fdTmp, idx + 2, strerror (errno), errno, GetLastError ());
                              fSameObj = 0;
                            }

                          /* Boost the thread priority. */
                          int const iPrioOld = GetThreadPriority (hThread);
                          int const iPrioNew = iPrioOld < THREAD_PRIORITY_NORMAL  ? THREAD_PRIORITY_NORMAL
                                             : iPrioOld < THREAD_PRIORITY_HIGHEST ? THREAD_PRIORITY_HIGHEST
                                             : iPrioOld;
                          if (iPrioOld != iPrioNew)
                            SetThreadPriority (hThread, iPrioNew);

                          /* Update the standard handle and close the temporary file descriptor. */
                          close (fdTmp);
                          return;
                        }
                      g_pipe_workarounds[idx].fShutdown = 1;
                      fprintf (stderr, "%s: warning: _dup2(%d,%d) failed: %s (%d, %u)",
                               program, fdTmp, idx + 1, strerror (errno), errno, GetLastError ());
                      for (unsigned msWait = 64; msWait <= 1000; msWait *= 2) /* wait almost 2 seconds. */
                        {
                          if (g_pipe_workarounds[idx].hSrcPipe != NULL)
                            CancelIoEx (g_pipe_workarounds[idx].hSrcPipe, NULL);
                          DWORD dwWait = WaitForSingleObject (hThread, msWait);
                          if (dwWait == WAIT_OBJECT_0)
                            break;
                        }
                      CloseHandle (g_pipe_workarounds[idx].hThread);
                    }
                  else
                    fprintf (stderr, "%s: warning: _beginthreadex failed: %s (%d, %u)",
                             program, strerror (errno), errno, GetLastError ());
                  CloseHandle (hDstPipe);
                }
              else
                fprintf (stderr, "%s: warning: DuplicateHandle failed: %u", program, GetLastError ());
            }
          else
            fprintf (stderr, "%s: warning: CreateEventW failed: %u", program, GetLastError ());
          close (fdTmp);
        }
      else
        {
          fprintf (stderr, "%s: warning: _open_osfhandle failed: %s (%d, %u)",
                   program, strerror (errno), errno, GetLastError ());
          CloseHandle (hPipeW);
        }
      CloseHandle (hPipeR);
    }
  else
    fprintf (stderr, "%s: warning: CreatePipe failed: %u", program, GetLastError());
}

/* Check if the two handles refers to the same pipe. */
int win_pipe_is_same_object (HANDLE hPipe1, HANDLE hPipe2)
{
  if (hPipe1 == NULL || hPipe1 == INVALID_HANDLE_VALUE)
    return 0;
  if (hPipe1 == hPipe2)
    return 1;

  /* Since windows 10 there is an API for this. */
  typedef BOOL (WINAPI *PFNCOMPAREOBJECTHANDLES)(HANDLE, HANDLE);
  static int                     s_fInitialized = 0;
  static PFNCOMPAREOBJECTHANDLES s_pfnCompareObjectHandles = NULL;
  PFNCOMPAREOBJECTHANDLES pfnCompareObjectHandles = s_pfnCompareObjectHandles;
  if (!pfnCompareObjectHandles && !s_fInitialized)
    {
      pfnCompareObjectHandles = (PFNCOMPAREOBJECTHANDLES)GetProcAddress (GetModuleHandleW (L"kernelbase.dll"),
                                                                         "CompareObjectHandles");
      s_pfnCompareObjectHandles = pfnCompareObjectHandles;
      s_fInitialized = 1;
    }
  if (pfnCompareObjectHandles)
    return pfnCompareObjectHandles (hPipe1, hPipe2);

  /* Otherwise we use FileInternalInformation, assuming ofc that the two are
     local pipes. */
  birdResolveImportsWorker();
  MY_IO_STATUS_BLOCK           Ios   = {0};
  MY_FILE_INTERNAL_INFORMATION Info1;
  MY_NTSTATUS rcNt = g_pfnNtQueryInformationFile (hPipe1, &Ios, &Info1, sizeof(Info1), MyFileInternalInformation);
  if (!NT_SUCCESS (rcNt))
    return 0;

  MY_FILE_INTERNAL_INFORMATION Info2;
  rcNt = g_pfnNtQueryInformationFile (hPipe2, &Ios, &Info2, sizeof(Info2), MyFileInternalInformation);
  if (!NT_SUCCESS (rcNt))
    return 0;

  return Info1.IndexNumber.QuadPart == Info2.IndexNumber.QuadPart;
}

/* Predicate function that checks if the hack is required. */
static int win_pipe_hack_needed (HANDLE hPipe, const char *pszEnvVarOverride)
{
  birdResolveImportsWorker ();

  /* Check the environment variable override first.
     Setting it to '0' disables the hack, setting to anything else (other than
     an empty string) forces the hack to be enabled. */
  const char * const pszValue = getenv (pszEnvVarOverride);
  if (pszValue && *pszValue != '\0')
    return *pszValue != '0';

  /* Check whether it is a pipe next. */
  DWORD const fType = GetFileType (hPipe) & ~FILE_TYPE_REMOTE;
  if (fType != FILE_TYPE_PIPE)
    return 0;

  /* Check if the pipe is synchronous or overlapping. If it's overlapping
     we must apply the workaround. */
  MY_IO_STATUS_BLOCK Ios = {0};
  DWORD fFlags = 0;
  MY_NTSTATUS rcNt = g_pfnNtQueryInformationFile (hPipe, &Ios, &fFlags, sizeof(fFlags), MyFileModeInformation);
  if (   NT_SUCCESS(rcNt)
      && !(fFlags & (FILE_SYNCHRONOUS_IO_NONALERT | FILE_SYNCHRONOUS_IO_ALERT)))
    return 1;

#if 1
  /* We could also check if the pipe is in NOWAIT mode, but since we've got
     code elsewhere for switching them to WAIT mode, we probably don't need
     to do that... */
  if (   GetNamedPipeHandleStateW (hPipe, &fFlags, NULL, NULL, NULL, NULL, 0)
      && (fFlags & PIPE_NOWAIT))
    return 1;
#endif
  return 0;
}

/** Initializes the pipe hack. */
static void win_pipe_hack_init (void)
{
  HANDLE const hStdOut       = (HANDLE)_get_osfhandle (_fileno (stdout));
  int const    fStdOutNeeded = win_pipe_hack_needed (hStdOut, "KMK_PIPE_HACK_STDOUT");
  HANDLE const hStdErr       = (HANDLE)_get_osfhandle (_fileno (stderr));
  int const    fStdErrNeeded = win_pipe_hack_needed (hStdErr, "KMK_PIPE_HACK_STDERR");

  /* To avoid getting too mixed up output in a 'kmk |& tee log' situation, we
     must try figure out if the two handles refer to the same pipe object. */
  int const    fSameObj      = fStdOutNeeded
                            && fStdErrNeeded
                            && win_pipe_is_same_object (hStdOut, hStdOut);

  /* Apply the hack as needed. */
  if (fStdOutNeeded)
    win_pipe_hack_apply (hStdOut, 0, fSameObj);
  if (fStdErrNeeded && !fSameObj)
    win_pipe_hack_apply (hStdErr, 1, 0);
  if (getenv ("KMK_PIPE_HACK_DEBUG"))
    fprintf (stderr, "fStdOutNeeded=%d fStdErrNeeded=%d fSameObj=%d\n",
             fStdOutNeeded, fStdErrNeeded, fSameObj);
}

#endif /* KBUILD_OS_WINDOWS && KMK */

#if defined(KMK) && !defined(NO_OUTPUT_SYNC)
/* Non-negative if we're counting output lines.

   This is used by die_with_job_output to decide whether the initial build
   error needs to be repeated because there was too much output from parallel
   jobs between it and the actual make termination. */
int output_metered = -1;

static void meter_output_block (char const *buffer, size_t len)
{
  while (len > 0)
    {
      char *nl = (char *)memchr (buffer, '\n', len);
      size_t linelen;
      if (nl)
        {
          linelen = nl - buffer + 1;
          output_metered++;
        }
      else
          linelen = len;
      output_metered += linelen / 132;

      /* advance */
      buffer += linelen;
      len    -= linelen;
    }
}
#endif


#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
# define MEMBUF_MIN_SEG_SIZE  4096
# define MEMBUF_MAX_SEG_SIZE  (512*1024)
# define MEMBUF_MAX_MOVE_LEN  (  MEMBUF_MIN_SEG_SIZE \
                               - offsetof (struct output_segment, runs) \
                               - sizeof (struct output_run))
# define MEMBUF_MAX_TOTAL     (  sizeof (void *) <= 4 \
                               ? (size_t)512*1024 : (size_t)16*1024*1024 )

static void *acquire_semaphore (void);
static void  release_semaphore (void *);
static int   log_working_directory (int);

/* Is make's stdout going to the same place as stderr?
   Also, did we already sync_init (== -1)?  */
static int combined_output = -1;

/* Helper for membuf_reset and output_reset */
static membuf_reset (struct output *out)
{
  struct output_segment *seg;
  while ((seg = out->out.head_seg))
    {
     out->out.head_seg = seg->next;
     free (seg);
    }
  out->out.tail_seg = NULL;
  out->out.tail_run = NULL;
  out->out.head_run = NULL;
  out->out.left     = 0;
  out->out.total    = 0;

  while ((seg = out->err.head_seg))
    {
     out->err.head_seg = seg->next;
     free (seg);
    }
  out->err.tail_seg = NULL;
  out->err.tail_run = NULL;
  out->err.head_run = NULL;
  out->err.left     = 0;
  out->err.total    = 0;

  out->seqno = 0;
}

/* Used by die_with_job_output to suppress output when it shouldn't be repeated. */
void output_reset (struct output *out)
{
  if (out && (out->out.total || out->err.total))
    membuf_reset (out);
}

/* Internal worker for output_dump and membuf_dump_most. */
static void membuf_dump (struct output *out)
{
  if (out->out.total || out->err.total)
    {
      int traced = 0;
      struct output_run *err_run;
      struct output_run *out_run;
      FILE *prevdst;

      /* Try to acquire the semaphore.  If it fails, dump the output
         unsynchronized; still better than silently discarding it.
         We want to keep this lock for as little time as possible.  */
      void *sem = acquire_semaphore ();
# if defined (KBUILD_OS_WINDOWS) || defined (KBUILD_OS_OS2) || defined (KBUILD_OS_DOS)
      int prev_mode_out = _setmode (fileno (stdout), _O_BINARY);
      int prev_mode_err = _setmode (fileno (stderr), _O_BINARY);
# endif

# ifndef KMK /* this drives me bananas. */
      /* Log the working directory for this dump.  */
      if (print_directory_flag && output_sync != OUTPUT_SYNC_RECURSE)
        traced = log_working_directory (1);
# endif

      /* Work the out and err sequences in parallel. */
      out_run = out->out.head_run;
      err_run = out->err.head_run;
      prevdst = NULL;
      while (err_run || out_run)
        {
          FILE       *dst;
          const void *src;
          size_t      len;
          if (out_run && (!err_run || out_run->seqno <= err_run->seqno))
            {
              src = out_run + 1;
              len = out_run->len;
              dst = stdout;
              out_run = out_run->next;
            }
          else
            {
              src = err_run + 1;
              len = err_run->len;
              dst = stderr;
              err_run = err_run->next;
            }
          if (dst != prevdst)
            fflush (prevdst);
          prevdst = dst;
#ifdef KMK
          if (output_metered < 0)
            { /* likely */ }
          else
            meter_output_block (src, len);
#endif
# if 0 /* for debugging */
          while (len > 0)
            {
              const char *nl = (const char *)memchr (src, '\n', len);
              size_t line_len = nl ? nl - (const char *)src + 1 : len;
              char *tmp = (char *)xmalloc (1 + line_len + 1 + 1);
              tmp[0] = '{';
              memcpy (&tmp[1], src, line_len);
              tmp[1 + line_len] = '}';
#  ifdef KBUILD_OS_WINDOWS
              maybe_con_fwrite (tmp, 1 + line_len + 1, 1, dst);
#  else
              fwrite (tmp, 1 + line_len + 1, 1, dst);
#  endif
              free (tmp);
              src  = (const char *)src + line_len;
              len -= line_len;
            }
#else
#  ifdef KBUILD_OS_WINDOWS
          maybe_con_fwrite (src, len, 1, dst);
#  else
          fwrite (src, len, 1, dst);
#  endif
# endif
        }
      if (prevdst)
        fflush (prevdst);

# ifndef KMK /* this drives me bananas. */
      if (traced)
        log_working_directory (0);
# endif

      /* Exit the critical section.  */
# if defined (KBUILD_OS_WINDOWS) || defined (KBUILD_OS_OS2) || defined (KBUILD_OS_DOS)
      if (prev_mode_out != -1)
        _setmode (fileno (stdout), prev_mode_out);
      if (prev_mode_err != -1)
        _setmode (fileno (stderr), prev_mode_err);
# endif
      if (sem)
        release_semaphore (sem);

# ifdef KMK
      if (!out->dont_truncate)
        { /* likely */ }
      else return;
# endif

      /* Free the segments and reset the state. */
      membuf_reset (out);
    }
  else
    assert (out->out.head_seg == NULL && out->err.head_seg == NULL);
}

/* Writes up to LEN bytes to the given segment.
   Returns how much was actually written.  */
static size_t
membuf_write_segment (struct output_membuf *membuf, struct output_segment *seg,
                      const char *src, size_t len, unsigned int *pseqno)
{
  size_t written = 0;
  if (seg && membuf->left > 0)
    {
      struct output_run *run = membuf->tail_run;
      char  *dst = (char *)(run + 1) + run->len;
      assert ((uintptr_t)run - (uintptr_t)seg < seg->size);

      /* If the sequence number didn't change, then we can append
         to the current run without further considerations. */
      if (run->seqno == *pseqno)
          written = len;
      /* If the current run does not end with a newline, don't start a new
         run till we encounter one. */
      else if (dst[-1] != '\n')
        {
          char const *srcnl = (const char *)memchr (src, '\n', len);
          written = srcnl ? srcnl - src + 1 : len;
        }
      /* Try create a new empty run and append to it. */
      else
        {
          size_t const offnextrun = (  (uintptr_t)dst - (uintptr_t)(seg)
                                     + sizeof(void *) - 1)
                                  & ~(sizeof(void *) - 1);
          if (offnextrun > seg->size - sizeof (struct output_run) * 2)
            return 0; /* need new segment */

          run = run->next = (struct output_run *)((char *)seg + offnextrun);
          run->next  = NULL;
          run->seqno = ++(*pseqno);
          run->len   = 0;
          membuf->tail_run = run;
          membuf->left = seg->size - (offnextrun + sizeof (*run));
          dst = (char *)(run + 1);
          written = len;
        }

      /* Append to the current run. */
      if (written > membuf->left)
        written = membuf->left;
      memcpy (dst, src, written);
      run->len += written;
      membuf->left -= written;
    }
  return written;
}

/* Helper for membuf_write_new_segment and membuf_dump_most that figures out
   now much data needs to be moved from the previous run in order to make it
   end with a newline.  */
static size_t membuf_calc_move_len (struct output_run *tail_run)
{
  size_t to_move = 0;
  if (tail_run)
    {
      const char *data = (const char *)(tail_run + 1);
      size_t off = tail_run->len;
      while (off > 0 && data[off - 1] != '\n')
        off--;
      to_move = tail_run->len - off;
      if (to_move  >=  MEMBUF_MAX_MOVE_LEN)
        to_move = 0;
    }
  return to_move;
}

/* Allocates a new segment and writes to it.
   This will take care to make sure the previous run terminates with
   a newline so that we pass whole lines to fwrite when dumping. */
static size_t
membuf_write_new_segment (struct output_membuf *membuf, const char *src,
                          size_t len, unsigned int *pseqno)
{
  struct output_run     *prev_run = membuf->tail_run;
  struct output_segment *prev_seg = membuf->tail_seg;
  size_t const           to_move  = membuf_calc_move_len (prev_run);
  struct output_segment *new_seg;
  size_t written;
  char *dst;

  /* Figure the the segment size.  We start with MEMBUF_MIN_SEG_SIZE and double
     it each time till we reach MEMBUF_MAX_SEG_SIZE. */
  size_t const offset_runs = offsetof (struct output_segment, runs);
  size_t segsize = !prev_seg ? MEMBUF_MIN_SEG_SIZE
                 : prev_seg->size >= MEMBUF_MAX_SEG_SIZE ? MEMBUF_MAX_SEG_SIZE
                 : prev_seg->size * 2;
  while (   segsize < to_move + len + offset_runs + sizeof (struct output_run) * 2
         && segsize < MEMBUF_MAX_SEG_SIZE)
    segsize *= 2;

  /* Allocate the segment and link it and the first run. */
  new_seg = (struct output_segment *)xmalloc (segsize);
  new_seg->size = segsize;
  new_seg->next = NULL;
  new_seg->runs[0].next = NULL;
  if (!prev_seg)
    {
      membuf->head_seg = new_seg;
      membuf->head_run = &new_seg->runs[0];
    }
  else
    {
      prev_seg->next = new_seg;
      prev_run->next = &new_seg->runs[0];
    }
  membuf->tail_seg = new_seg;
  membuf->tail_run = &new_seg->runs[0];
  membuf->total += segsize;
  membuf->left = segsize - sizeof (struct output_run) - offset_runs;

  /* Initialize and write data to the first run. */
  dst = (char *)&new_seg->runs[0]; /* Try bypass gcc array size cleverness. */
  dst += sizeof (struct output_run);
  assert (MEMBUF_MAX_MOVE_LEN < MEMBUF_MIN_SEG_SIZE);
  if (to_move > 0)
    {
      /* Move to_move bytes from the previous run in hope that we'll get a
         newline to soon.  Afterwards call membuf_segment_write to work SRC. */
      assert (prev_run != NULL);
      assert (membuf->left >= to_move);
      prev_run->len -= to_move;
      new_seg->runs[0].len = to_move;
      new_seg->runs[0].seqno = prev_run->seqno;
      memcpy (dst, (const char *)(prev_run + 1) + prev_run->len, to_move);
      membuf->left -= to_move;

      written = membuf_write_segment (membuf, new_seg, src, len, pseqno);
    }
  else
    {
      /* Create a run with up to LEN from SRC. */
      written = len;
      if (written > membuf->left)
        written = membuf->left;
      new_seg->runs[0].len = written;
      new_seg->runs[0].seqno = ++(*pseqno);
      memcpy (dst, src, written);
      membuf->left -= written;
    }
  return written;
}

/* Worker for output_write that will dump most of the output when we hit
   MEMBUF_MAX_TOTAL on either of the two membuf structures, then free all the
   output segments.  Incomplete lines will be held over to the next buffers
   and copied into new segments. */
static void
membuf_dump_most (struct output *out)
{
  size_t out_to_move = membuf_calc_move_len (out->out.tail_run);
  size_t err_to_move = membuf_calc_move_len (out->err.tail_run);
  if (!out_to_move && !err_to_move)
    membuf_dump (out);
  else
    {
      /* Allocate a stack buffer for holding incomplete lines.  This should be
         fine since we're only talking about max 2 * MEMBUF_MAX_MOVE_LEN.
         The -1 on the sequence numbers, ise because membuf_write_new_segment
         will increment them before use. */
      unsigned int out_seqno = out_to_move ? out->out.tail_run->seqno - 1 : 0;
      unsigned int err_seqno = err_to_move ? out->err.tail_run->seqno - 1 : 0;
      char *tmp = alloca (out_to_move + err_to_move);
      if (out_to_move)
        {
          out->out.tail_run->len -= out_to_move;
          memcpy (tmp,
                  (char *)(out->out.tail_run + 1) + out->out.tail_run->len,
                  out_to_move);
        }
      if (err_to_move)
        {
          out->err.tail_run->len -= err_to_move;
          memcpy (tmp + out_to_move,
                  (char *)(out->err.tail_run + 1) + out->err.tail_run->len,
                  err_to_move);
        }

      membuf_dump (out);

      if (out_to_move)
        {
          size_t written = membuf_write_new_segment (&out->out, tmp,
                                                     out_to_move, &out_seqno);
          assert (written == out_to_move); (void)written;
        }
      if (err_to_move)
        {
          size_t written = membuf_write_new_segment (&out->err,
                                                     tmp + out_to_move,
                                                     err_to_move, &err_seqno);
          assert (written == err_to_move); (void)written;
        }
    }
}


/* write/fwrite like function, binary mode. */
ssize_t
output_write_bin (struct output *out, int is_err, const char *src, size_t len)
{
  size_t ret = len;
  if (!out || !out->syncout)
    {
      FILE *f = is_err ? stderr : stdout;
# if defined (KBUILD_OS_WINDOWS) || defined (KBUILD_OS_OS2) || defined (KBUILD_OS_DOS)
      /* On DOS platforms we need to disable \n -> \r\n converts that is common on
         standard output/error.  Also optimize for console output. */
      int saved_errno;
      int fd = fileno (f);
      int prev_mode = _setmode (fd, _O_BINARY);
      maybe_con_fwrite (src, len, 1, f);
      if (fflush (f) == EOF)
        ret = -1;
      saved_errno = errno;
      _setmode (fd, prev_mode);
      errno = saved_errno;
# else
      fwrite (src, len, 1, f);
      if (fflush (f) == EOF)
        ret = -1;
# endif
    }
  else
    {
      struct output_membuf *membuf = is_err ? &out->err : &out->out;
      while (len > 0)
        {
          size_t runlen = membuf_write_segment (membuf, membuf->tail_seg, src, len, &out->seqno);
          if (!runlen)
            {
              if (membuf->total < MEMBUF_MAX_TOTAL)
                runlen = membuf_write_new_segment (membuf, src, len, &out->seqno);
              else
                membuf_dump_most (out);
            }
          /* advance */
          len -= runlen;
          src += runlen;
        }
    }
  return ret;
}

#endif /* CONFIG_WITH_OUTPUT_IN_MEMORY */

/* write/fwrite like function, text mode. */
ssize_t
output_write_text (struct output *out, int is_err, const char *src, size_t len)
{
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
# if defined (KBUILD_OS_WINDOWS) || defined (KBUILD_OS_OS2) || defined (KBUILD_OS_DOS)
  ssize_t ret = len;
  if (!out || !out->syncout)
    {
      /* ASSUME fwrite does the desired conversion. */
      FILE *f = is_err ? stderr : stdout;
# ifdef KBUILD_OS_WINDOWS
      if (maybe_con_fwrite (src, len, 1, f) < 0)
        ret = -1;
# else
      fwrite (src, len, 1, f);
# endif
      if (fflush (f) == EOF)
        ret = -1;
    }
  else
    {
      /* Work the buffer line by line, replacing each \n with \r\n. */
      while (len > 0)
        {
          const char *nl = memchr (src, '\n', len);
          size_t line_len = nl ? nl - src : len;
          output_write_bin (out, is_err, src, line_len);
          if (!nl)
              break;
          output_write_bin (out, is_err, "\r\n", 2);
          len -= line_len + 1;
          src += line_len + 1;
        }
    }
  return ret;
# else
  return output_write_bin (out, is_err, src, len);
# endif
#else
  ssize_t ret = len;
  if (! out || ! out->syncout)
    {
      FILE *f = is_err ? stderr : stdout;
# ifdef KBUILD_OS_WINDOWS
      maybe_con_fwrite(src, len, 1, f);
# else
      fwrite (src, len, 1, f);
# endif
      fflush (f);
    }
  else
    {
      int fd = is_err ? out->err : out->out;
      int r;

      EINTRLOOP (r, lseek (fd, 0, SEEK_END));
      while (1)
        {
          EINTRLOOP (r, write (fd, src, len));
          if ((size_t)r == len || r <= 0)
            break;
          len -= r;
          src += r;
        }
    }
  return ret;
#endif
}



/* Write a string to the current STDOUT or STDERR.  */
static void
_outputs (struct output *out, int is_err, const char *msg)
{
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
  output_write_text (out, is_err, msg, strlen (msg));
#else  /* !CONFIG_WITH_OUTPUT_IN_MEMORY */
  if (! out || ! out->syncout)
    {
      FILE *f = is_err ? stderr : stdout;
# ifdef KBUILD_OS_WINDOWS
      maybe_con_fwrite(msg, strlen(msg), 1, f);
# else
      fputs (msg, f);
# endif
      fflush (f);
    }
  else
    {
      int fd = is_err ? out->err : out->out;
      int len = strlen (msg);
      int r;

      EINTRLOOP (r, lseek (fd, 0, SEEK_END));
      while (1)
        {
          EINTRLOOP (r, write (fd, msg, len));
          if (r == len || r <= 0)
            break;
          len -= r;
          msg += r;
        }
    }
#endif /* !CONFIG_WITH_OUTPUT_IN_MEMORY */
}

/* Write a message indicating that we've just entered or
   left (according to ENTERING) the current directory.  */

static int
log_working_directory (int entering)
{
  static char *buf = NULL;
  static unsigned int len = 0;
  unsigned int need;
  const char *fmt;
  char *p;

  /* Get enough space for the longest possible output.  */
  need = strlen (program) + INTSTR_LENGTH + 2 + 1;
  if (starting_directory)
    need += strlen (starting_directory);

  /* Use entire sentences to give the translators a fighting chance.  */
  if (makelevel == 0)
    if (starting_directory == 0)
      if (entering)
        fmt = _("%s: Entering an unknown directory\n");
      else
        fmt = _("%s: Leaving an unknown directory\n");
    else
      if (entering)
        fmt = _("%s: Entering directory '%s'\n");
      else
        fmt = _("%s: Leaving directory '%s'\n");
  else
    if (starting_directory == 0)
      if (entering)
        fmt = _("%s[%u]: Entering an unknown directory\n");
      else
        fmt = _("%s[%u]: Leaving an unknown directory\n");
    else
      if (entering)
        fmt = _("%s[%u]: Entering directory '%s'\n");
      else
        fmt = _("%s[%u]: Leaving directory '%s'\n");

  need += strlen (fmt);

  if (need > len)
    {
      buf = xrealloc (buf, need);
      len = need;
    }

  p = buf;
  if (print_data_base_flag)
    {
      *(p++) = '#';
      *(p++) = ' ';
    }

  if (makelevel == 0)
    if (starting_directory == 0)
      sprintf (p, fmt , program);
    else
      sprintf (p, fmt, program, starting_directory);
  else if (starting_directory == 0)
    sprintf (p, fmt, program, makelevel);
  else
    sprintf (p, fmt, program, makelevel, starting_directory);

  _outputs (NULL, 0, buf);

  return 1;
}

/* Set a file descriptor to be in O_APPEND mode.
   If it fails, just ignore it.  */

static void
set_append_mode (int fd)
{
#if defined(F_GETFL) && defined(F_SETFL) && defined(O_APPEND)
  int flags = fcntl (fd, F_GETFL, 0);
  if (flags >= 0)
    fcntl (fd, F_SETFL, flags | O_APPEND);
#endif
}


#ifndef NO_OUTPUT_SYNC

/* Semaphore for use in -j mode with output_sync. */
static sync_handle_t sync_handle = -1;

#define FD_NOT_EMPTY(_f) ((_f) != OUTPUT_NONE && lseek ((_f), 0, SEEK_END) > 0)

/* Set up the sync handle.  Disables output_sync on error.  */
static int
sync_init (void)
{
  int combined_output = 0;

#ifdef WINDOWS32
# ifdef CONFIG_NEW_WIN_CHILDREN
  if (STREAM_OK (stdout))
    {
      if (STREAM_OK (stderr))
        {
          char mtxname[256];
          sync_handle = create_mutex (mtxname, sizeof (mtxname));
          if (sync_handle != -1)
            {
              prepare_mutex_handle_string (mtxname);
              return same_stream (stdout, stderr);
            }
          perror_with_name ("output-sync suppressed: ", "create_mutex");
        }
      else
        perror_with_name ("output-sync suppressed: ", "stderr");
    }
  else
    perror_with_name ("output-sync suppressed: ", "stdout");
  output_sync = OUTPUT_SYNC_NONE;

# else  /* !CONFIG_NEW_WIN_CHILDREN */
  if ((!STREAM_OK (stdout) && !STREAM_OK (stderr))
      || (sync_handle = create_mutex ()) == -1)
    {
      perror_with_name ("output-sync suppressed: ", "stderr");
      output_sync = 0;
    }
  else
    {
      combined_output = same_stream (stdout, stderr);
      prepare_mutex_handle_string (sync_handle);
    }
# endif /* !CONFIG_NEW_WIN_CHILDREN */

#else
  if (STREAM_OK (stdout))
    {
      struct stat stbuf_o, stbuf_e;

      sync_handle = fileno (stdout);
      combined_output = (fstat (fileno (stdout), &stbuf_o) == 0
                         && fstat (fileno (stderr), &stbuf_e) == 0
                         && stbuf_o.st_dev == stbuf_e.st_dev
                         && stbuf_o.st_ino == stbuf_e.st_ino);
    }
  else if (STREAM_OK (stderr))
    sync_handle = fileno (stderr);
  else
    {
      perror_with_name ("output-sync suppressed: ", "stderr");
      output_sync = 0;
    }
#endif

  return combined_output;
}

#ifndef CONFIG_WITH_OUTPUT_IN_MEMORY
/* Support routine for output_sync() */
static void
pump_from_tmp (int from, FILE *to)
{
# ifdef KMK
  char buffer[8192];
# else
  static char buffer[8192];
#endif

# if defined (KBUILD_OS_WINDOWS) || defined (KBUILD_OS_OS2) || defined (KBUILD_OS_DOS)
  int prev_mode;

  /* "from" is opened by open_tmpfd, which does it in binary mode, so
     we need the mode of "to" to match that.  */
  prev_mode = _setmode (fileno (to), O_BINARY);
#endif

  if (lseek (from, 0, SEEK_SET) == -1)
    perror ("lseek()");

  while (1)
    {
      int len;
      EINTRLOOP (len, read (from, buffer, sizeof (buffer)));
      if (len < 0)
        perror ("read()");
      if (len <= 0)
        break;
#ifdef KMK
      if (output_metered < 0)
        { /* likely */ }
      else
        meter_output_block (buffer, len);
#endif
      if (fwrite (buffer, len, 1, to) < 1)
        {
          perror ("fwrite()");
          break;
        }
      fflush (to);
    }

# if defined (KBUILD_OS_WINDOWS) || defined (KBUILD_OS_OS2) || defined (KBUILD_OS_DOS)
  /* Switch "to" back to its original mode, so that log messages by
     Make have the same EOL format as without --output-sync.  */
  _setmode (fileno (to), prev_mode);
#endif
}
#endif /* CONFIG_WITH_OUTPUT_IN_MEMORY */

/* Obtain the lock for writing output.  */
static void *
acquire_semaphore (void)
{
  static struct flock fl;

  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 1;
  if (fcntl (sync_handle, F_SETLKW, &fl) != -1)
    return &fl;
#ifdef KBUILD_OS_DARWIN /* F_SETLKW isn't supported on pipes */
  if (errno != EBADF)
#endif  
  perror ("fcntl()");
  return NULL;
}

/* Release the lock for writing output.  */
static void
release_semaphore (void *sem)
{
  struct flock *flp = (struct flock *)sem;
  flp->l_type = F_UNLCK;
  if (fcntl (sync_handle, F_SETLKW, flp) == -1)
    perror ("fcntl()");
}

#ifndef CONFIG_WITH_OUTPUT_IN_MEMORY

/* Returns a file descriptor to a temporary file.  The file is automatically
   closed/deleted on exit.  Don't use a FILE* stream.  */
int
output_tmpfd (void)
{
  int fd = -1;
  FILE *tfile = tmpfile ();

  if (! tfile)
    {
#ifdef KMK
      if (output_context && output_context->syncout)
        output_context->syncout = 0; /* Avoid inifinit recursion. */
#endif
      pfatal_with_name ("tmpfile");
    }

  /* Create a duplicate so we can close the stream.  */
  fd = dup (fileno (tfile));
  if (fd < 0)
    {
#ifdef KMK
      if (output_context && output_context->syncout)
        output_context->syncout = 0; /* Avoid inifinit recursion. */
#endif
      pfatal_with_name ("dup");
    }

  fclose (tfile);

  set_append_mode (fd);

  return fd;
}

/* Adds file descriptors to the child structure to support output_sync; one
   for stdout and one for stderr as long as they are open.  If stdout and
   stderr share a device they can share a temp file too.
   Will reset output_sync on error.  */
static void
setup_tmpfile (struct output *out)
{
  /* Is make's stdout going to the same place as stderr?  */
  static int combined_output = -1;

  if (combined_output < 0)
    {
#ifdef KMK /* prevent infinite recursion if sync_init() calls perror_with_name. */
      combined_output = 0;
#endif
      combined_output = sync_init ();
    }

  if (STREAM_OK (stdout))
    {
      int fd = output_tmpfd ();
      if (fd < 0)
        goto error;
      CLOSE_ON_EXEC (fd);
      out->out = fd;
    }

  if (STREAM_OK (stderr))
    {
      if (out->out != OUTPUT_NONE && combined_output)
        out->err = out->out;
      else
        {
          int fd = output_tmpfd ();
          if (fd < 0)
            goto error;
          CLOSE_ON_EXEC (fd);
          out->err = fd;
        }
    }

  return;

  /* If we failed to create a temp file, disable output sync going forward.  */
 error:
  output_close (out);
  output_sync = OUTPUT_SYNC_NONE;
}

#endif /* !CONFIG_WITH_OUTPUT_IN_MEMORY */

/* Synchronize the output of jobs in -j mode to keep the results of
   each job together. This is done by holding the results in temp files,
   one for stdout and potentially another for stderr, and only releasing
   them to "real" stdout/stderr when a semaphore can be obtained. */

void
output_dump (struct output *out)
{
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
  membuf_dump (out);
#else
  int outfd_not_empty = FD_NOT_EMPTY (out->out);
  int errfd_not_empty = FD_NOT_EMPTY (out->err);

  if (outfd_not_empty || errfd_not_empty)
    {
# ifndef KMK /* this drives me bananas. */
      int traced = 0;
# endif

      /* Try to acquire the semaphore.  If it fails, dump the output
         unsynchronized; still better than silently discarding it.
         We want to keep this lock for as little time as possible.  */
      void *sem = acquire_semaphore ();

# ifndef KMK /* this drives me bananas. */
      /* Log the working directory for this dump.  */
      if (print_directory_flag && output_sync != OUTPUT_SYNC_RECURSE)
        traced = log_working_directory (1);
# endif

      if (outfd_not_empty)
        pump_from_tmp (out->out, stdout);
      if (errfd_not_empty && out->err != out->out)
        pump_from_tmp (out->err, stderr);

# ifndef KMK /* this drives me bananas. */
      if (traced)
        log_working_directory (0);
# endif

      /* Exit the critical section.  */
      if (sem)
        release_semaphore (sem);

# ifdef KMK
      if (!out->dont_truncate)
        { /* likely */ }
      else return;
# endif
      /* Truncate and reset the output, in case we use it again.  */
      if (out->out != OUTPUT_NONE)
        {
          int e;
          lseek (out->out, 0, SEEK_SET);
          EINTRLOOP (e, ftruncate (out->out, 0));
        }
      if (out->err != OUTPUT_NONE && out->err != out->out)
        {
          int e;
          lseek (out->err, 0, SEEK_SET);
          EINTRLOOP (e, ftruncate (out->err, 0));
        }
    }
#endif
}

# if defined(KMK) && !defined(CONFIG_WITH_OUTPUT_IN_MEMORY)
/* Used by die_with_job_output to suppress output when it shouldn't be repeated. */
void output_reset (struct output *out)
{
  if (out)
    {
      if (out->out != OUTPUT_NONE)
        {
          int e;
          lseek (out->out, 0, SEEK_SET);
          EINTRLOOP (e, ftruncate (out->out, 0));
        }
      if (out->err != OUTPUT_NONE && out->err != out->out)
        {
          int e;
          lseek (out->err, 0, SEEK_SET);
          EINTRLOOP (e, ftruncate (out->err, 0));
        }
    }
}
# endif
#endif /* NO_OUTPUT_SYNC */


/* Provide support for temporary files.  */

#ifndef HAVE_STDLIB_H
# ifdef HAVE_MKSTEMP
int mkstemp (char *template);
# else
char *mktemp (char *template);
# endif
#endif

FILE *
output_tmpfile (char **name, const char *template)
{
#ifdef HAVE_FDOPEN
  int fd;
#endif

#if defined HAVE_MKSTEMP || defined HAVE_MKTEMP
# define TEMPLATE_LEN   strlen (template)
#else
# define TEMPLATE_LEN   L_tmpnam
#endif
  *name = xmalloc (TEMPLATE_LEN + 1);
  strcpy (*name, template);

#if defined HAVE_MKSTEMP && defined HAVE_FDOPEN
  /* It's safest to use mkstemp(), if we can.  */
  fd = mkstemp (*name);
  if (fd == -1)
    return 0;
  return fdopen (fd, "w");
#else
# ifdef HAVE_MKTEMP
  (void) mktemp (*name);
# else
  (void) tmpnam (*name);
# endif

# ifdef HAVE_FDOPEN
  /* Can't use mkstemp(), but guard against a race condition.  */
  EINTRLOOP (fd, open (*name, O_CREAT|O_EXCL|O_WRONLY, 0600));
  if (fd == -1)
    return 0;
  return fdopen (fd, "w");
# else
  /* Not secure, but what can we do?  */
  return fopen (*name, "w");
# endif
#endif
}


/* This code is stolen from gnulib.
   If/when we abandon the requirement to work with K&R compilers, we can
   remove this (and perhaps other parts of GNU make!) and migrate to using
   gnulib directly.

   This is called only through atexit(), which means die() has already been
   invoked.  So, call exit() here directly.  Apparently that works...?
*/

/* Close standard output, exiting with status 'exit_failure' on failure.
   If a program writes *anything* to stdout, that program should close
   stdout and make sure that it succeeds before exiting.  Otherwise,
   suppose that you go to the extreme of checking the return status
   of every function that does an explicit write to stdout.  The last
   printf can succeed in writing to the internal stream buffer, and yet
   the fclose(stdout) could still fail (due e.g., to a disk full error)
   when it tries to write out that buffered data.  Thus, you would be
   left with an incomplete output file and the offending program would
   exit successfully.  Even calling fflush is not always sufficient,
   since some file systems (NFS and CODA) buffer written/flushed data
   until an actual close call.

   Besides, it's wasteful to check the return value from every call
   that writes to stdout -- just let the internal stream state record
   the failure.  That's what the ferror test is checking below.

   It's important to detect such failures and exit nonzero because many
   tools (most notably 'make' and other build-management systems) depend
   on being able to detect failure in other tools via their exit status.  */

static void
close_stdout (void)
{
  int prev_fail = ferror (stdout);
#ifdef DEBUG_STDOUT_CLOSE_ISSUE
  if (prev_fail)
    my_stdout_error ("close_stdout", "error pending on entry!");
  errno = 0; SetLastError (0);
#endif
  int fclose_fail = fclose (stdout);

  if (prev_fail || fclose_fail)
    {
#ifdef DEBUG_STDOUT_CLOSE_ISSUE
      if (fclose_fail)
        my_stdout_error ("close_stdout", "fclose failed!");
#endif
      if (fclose_fail)
        perror_with_name (_("write error: stdout"), "");
      else
        O (error, NILF, _("write error: stdout"));
      exit (MAKE_TROUBLE);
    }
#if defined(KBUILD_OS_WINDOWS) && defined(KMK)
  win_pipe_hack_terminate ();
#endif
}


void
output_init (struct output *out)
{
#if defined(KBUILD_OS_WINDOWS) && defined(KMK)
  /* Apply workaround for asynchronous pipes on windows on first call. */
  static int s_not_first_call = 0;
  if (!s_not_first_call)
    {
      s_not_first_call = 1;
      win_pipe_hack_init ();
    }
#endif

#ifdef DEBUG_STDOUT_CLOSE_ISSUE
  if (STREAM_OK (stdout) && ferror (stdout))
    my_stdout_error (out ? "output_init(out)" : "output_init(NULL)", "error pending entry!");
#endif

  if (out)
    {
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
      out->out.head_seg  = NULL;
      out->out.tail_seg  = NULL;
      out->out.head_run  = NULL;
      out->out.tail_run  = NULL;
      out->err.head_seg  = NULL;
      out->err.tail_seg  = NULL;
      out->err.head_run  = NULL;
      out->err.tail_run  = NULL;
      out->err.total     = 0;
      out->out.total     = 0;
      out->seqno         = 0;
#else
      out->out = out->err = OUTPUT_NONE;
#endif
      out->syncout = !!output_sync;
#ifdef KMK
      out->dont_truncate = 0;
#endif
      return;
    }

  /* Configure this instance of make.  Be sure stdout is line-buffered.  */

#ifdef HAVE_SETVBUF
# ifdef SETVBUF_REVERSED
  setvbuf (stdout, _IOLBF, xmalloc (BUFSIZ), BUFSIZ);
# else  /* setvbuf not reversed.  */
  /* Some buggy systems lose if we pass 0 instead of allocating ourselves.  */
  setvbuf (stdout, 0, _IOLBF, BUFSIZ);
# endif /* setvbuf reversed.  */
#elif HAVE_SETLINEBUF
  setlinebuf (stdout);
#endif  /* setlinebuf missing.  */

  /* Force stdout/stderr into append mode.  This ensures parallel jobs won't
     lose output due to overlapping writes.  */
  set_append_mode (fileno (stdout));
  set_append_mode (fileno (stderr));

#ifdef DEBUG_STDOUT_CLOSE_ISSUE
  if (ferror (stdout))
    my_stdout_error ("output_init", "error pending on exit!");
# ifdef KBUILD_OS_WINDOWS
  {
    HANDLE const hStdOut = (HANDLE)_get_osfhandle(_fileno(stdout));
    DWORD const  dwType  = GetFileType(hStdOut);
    birdResolveImportsWorker();
    if ((dwType & ~FILE_TYPE_REMOTE) == FILE_TYPE_PIPE)
      {
        my_output_pipe_info (hStdOut, "output_init");
#  if 0
        DWORD cbOutBuf      = 0;
        DWORD cbInBuf       = 0;
        BOOL const fRc2 = GetNamedPipeInfo(hStdOut, NULL, &cbOutBuf, &cbInBuf, NULL);
        if (cbInBuf != 0x1000)
          {
            DWORD dwMode = 0;
            if (GetNamedPipeHandleStateW(hStdOut, &dwMode, NULL, NULL, NULL, NULL, 0))
              {
                dwMode &= ~PIPE_WAIT;
                dwMode |= PIPE_NOWAIT;
                if (!SetNamedPipeHandleState(hStdOut, &dwMode, NULL, NULL))
                  fprintf(stderr, "SetNamedPipeHandleState failed: %u\n", GetLastError());
                else
                  {
                    GetNamedPipeHandleStateW(hStdOut, &dwMode, NULL, NULL, NULL, NULL, 0);
                    fprintf(stderr, "SetNamedPipeHandleState succeeded: %#x\n", dwMode);
                  }
              }
          }
#  endif
# endif
      }
    }
#endif
#ifdef HAVE_ATEXIT
  if (STREAM_OK (stdout))
    atexit (close_stdout);
#endif
}

void
output_close (struct output *out)
{
  if (! out)
    {
      if (stdio_traced)
        log_working_directory (0);
      return;
    }

#ifndef NO_OUTPUT_SYNC
  output_dump (out);
#endif

#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
  assert (out->out.total == 0);
  assert (out->out.head_seg == NULL);
  assert (out->err.total == 0);
  assert (out->err.head_seg == NULL);
#else
  if (out->out >= 0)
    close (out->out);
  if (out->err >= 0 && out->err != out->out)
    close (out->err);
#endif

  output_init (out);
}

/* We're about to generate output: be sure it's set up.  */
void
output_start (void)
{
#ifdef CONFIG_WITH_OUTPUT_IN_MEMORY
  /* If we're syncing output make sure the sempahore (win) is set up. */
  if (output_context && output_context->syncout)
    if (combined_output < 0)
      {
        combined_output = 0;
        combined_output = sync_init ();
      }
#else
#ifndef NO_OUTPUT_SYNC
  /* If we're syncing output make sure the temporary file is set up.  */
  if (output_context && output_context->syncout)
    if (! OUTPUT_ISSET(output_context))
      setup_tmpfile (output_context);
#endif
#endif

#ifndef KMK
  /* If we're not syncing this output per-line or per-target, make sure we emit
     the "Entering..." message where appropriate.  */
  if (output_sync == OUTPUT_SYNC_NONE || output_sync == OUTPUT_SYNC_RECURSE)
#else
  /* Indiscriminately output "Entering..." and "Leaving..." message for each
     command line or target is plain annoying!  And when there is no recursion
     it's actually inappropriate.   Haven't got a simple way of detecting that,
     so back to the old behavior for now.  [bird] */
#endif
    if (! stdio_traced && print_directory_flag)
      stdio_traced = log_working_directory (1);
}

void
outputs (int is_err, const char *msg)
{
  if (! msg || *msg == '\0')
    return;

  output_start ();

  _outputs (output_context, is_err, msg);
}


static struct fmtstring
  {
    char *buffer;
    size_t size;
  } fmtbuf = { NULL, 0 };

static char *
get_buffer (size_t need)
{
  /* Make sure we have room.  NEED includes space for \0.  */
  if (need > fmtbuf.size)
    {
      fmtbuf.size += need * 2;
      fmtbuf.buffer = xrealloc (fmtbuf.buffer, fmtbuf.size);
    }

  fmtbuf.buffer[need-1] = '\0';

  return fmtbuf.buffer;
}

/* Print a message on stdout.  */

void
message (int prefix, size_t len, const char *fmt, ...)
{
  va_list args;
  char *p;

  len += strlen (fmt) + strlen (program) + INTSTR_LENGTH + 4 + 1 + 1;
  p = get_buffer (len);

  if (prefix)
    {
      if (makelevel == 0)
        sprintf (p, "%s: ", program);
      else
        sprintf (p, "%s[%u]: ", program, makelevel);
      p += strlen (p);
    }

  va_start (args, fmt);
  vsprintf (p, fmt, args);
  va_end (args);

  strcat (p, "\n");

  assert (fmtbuf.buffer[len-1] == '\0');
  outputs (0, fmtbuf.buffer);
}

/* Print an error message.  */

void
error (const floc *flocp, size_t len, const char *fmt, ...)
{
  va_list args;
  char *p;

  len += (strlen (fmt) + strlen (program)
          + (flocp && flocp->filenm ? strlen (flocp->filenm) : 0)
          + INTSTR_LENGTH + 4 + 1 + 1);
  p = get_buffer (len);

  if (flocp && flocp->filenm)
    sprintf (p, "%s:%lu: ", flocp->filenm, flocp->lineno + flocp->offset);
  else if (makelevel == 0)
    sprintf (p, "%s: ", program);
  else
    sprintf (p, "%s[%u]: ", program, makelevel);
  p += strlen (p);

  va_start (args, fmt);
  vsprintf (p, fmt, args);
  va_end (args);

  strcat (p, "\n");

  assert (fmtbuf.buffer[len-1] == '\0');
  outputs (1, fmtbuf.buffer);
}

/* Print an error message and exit.  */

void
fatal (const floc *flocp, size_t len, const char *fmt, ...)
{
  va_list args;
  const char *stop = _(".  Stop.\n");
  char *p;

  len += (strlen (fmt) + strlen (program)
          + (flocp && flocp->filenm ? strlen (flocp->filenm) : 0)
          + INTSTR_LENGTH + 8 + strlen (stop) + 1);
  p = get_buffer (len);

  if (flocp && flocp->filenm)
    sprintf (p, "%s:%lu: *** ", flocp->filenm, flocp->lineno + flocp->offset);
  else if (makelevel == 0)
    sprintf (p, "%s: *** ", program);
  else
    sprintf (p, "%s[%u]: *** ", program, makelevel);
  p += strlen (p);

  va_start (args, fmt);
  vsprintf (p, fmt, args);
  va_end (args);

  strcat (p, stop);

  assert (fmtbuf.buffer[len-1] == '\0');
  outputs (1, fmtbuf.buffer);

  die (MAKE_FAILURE);
}

/* Print an error message from errno.  */

void
perror_with_name (const char *str, const char *name)
{
  const char *err = strerror (errno);
  OSSS (error, NILF, _("%s%s: %s"), str, name, err);
}

/* Print an error message from errno and exit.  */

void
pfatal_with_name (const char *name)
{
  const char *err = strerror (errno);
  OSS (fatal, NILF, _("%s: %s"), name, err);

  /* NOTREACHED */
}
