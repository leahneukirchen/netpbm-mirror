/*=============================================================================
                                 pm_system
===============================================================================
   This is the pm_system() family of subroutines.

   pm_system() is just like Standard C Library system(), except that you can
   supply routines for it to run to generate the Standard Input for the
   executed shell command and to accept the Standard Output from it.
   system(), by contrast, always sets up the current Standard Input and
   Standard Output as the Standard Input and Standard Output of the shell
   command.

   pm_system_lp() and pm_system_vp() are similar, but exec an OS-level program
   (i.e. exec a program) rather than run a shell command.

   By Bryan Henderson, San Jose CA  2002.12.14.

   Contributed to the public domain.
=============================================================================*/
#define _XOPEN_SOURCE
#define _BSD_SOURCE  /* Make SIGWINCH defined on OpenBSD */

#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "netpbm/pm_c_util.h"
#include "netpbm/mallocvar.h"
#include "netpbm/nstring.h"
#include "pm.h"
#include "pm_system.h"

#define STDIN 0
#define STDOUT 1


static void
closeUninheritableFds(int const stdinFd,
                      int const stdoutFd) {
/*----------------------------------------------------------------------------
  Close all the file descriptors that we declare uninheritable -- files Parent
  has open that Child has no business accessing.

  Closing an extra file descriptor is essential to allow the file to close
  when Parent closes it.

  We define uninheritable as less than 64 and not Standard Input, Output,
  or Error, or 'stdinFd' or 'stdoutFd'.
-----------------------------------------------------------------------------*/
    int fd;

    for (fd = 0; fd < 64; ++fd) {
        if (fd == stdinFd) {
        } else if (fd == stdoutFd) {
        } else if (fd == STDIN_FILENO) {
        } else if (fd == STDOUT_FILENO) {
        } else if (fd == STDERR_FILENO) {
        } else {
            close(fd);
        }
    }
}



static void
execProgram(const char *  const progName,
            const char ** const argArray,
            int           const stdinFd,
            int           const stdoutFd) {
/*----------------------------------------------------------------------------
   Exec the program 'progName' with arguments argArray[], with 'stdinFd' as
   its Standard Input and 'stdoutFd' as its Standard Output.

   But if the exec fails, leave all file descriptors as we found them.

   Note that stdinFd or stdoutFd may actually be Standard Input and
   Standard Output already.
-----------------------------------------------------------------------------*/
    int stdinSaveFd, stdoutSaveFd;
    int rc;
    int execErrno;

    /* Make stdinFd Standard Input.
       Make stdoutFd Standard Output.
    */
    if (stdinFd != STDIN) {
        stdinSaveFd  = dup(STDIN);
        close(STDIN);
        dup2(stdinFd, STDIN);
        close(stdinFd);
    }
    if (stdoutFd != STDOUT) {
        stdoutSaveFd = dup(STDOUT);
        close(STDOUT);
        dup2(stdoutFd, STDOUT);
        close(stdoutFd);
    }

    rc = execvp(progName, (char **)argArray);

    execErrno = errno;

    if (stdinFd != STDIN) {
        close(STDIN);
        dup2(stdinSaveFd, STDIN);
        close(stdinSaveFd);
    }
    if (stdoutFd != STDOUT) {
        close(STDOUT);
        dup2(stdoutSaveFd, STDOUT);
        close(stdoutSaveFd);
    }
    if (rc < 0)
        pm_error("Unable to exec '%s' "
                 "(i.e. the program did not run at all).  "
                 "execvp() errno=%d (%s)",
                 progName, execErrno, strerror(execErrno));
    else
        pm_error("INTERNAL ERROR.  execvp() returns, but does not fail.");
}



static void
createPipeFeeder(void          pipeFeederRtn(int, void *), 
                 void *  const feederParm, 
                 int *   const fdP,
                 pid_t * const pidP) {
/*----------------------------------------------------------------------------
   Create a process and a pipe.  Have the process run program
   'pipeFeederRtn' to fill the pipe and return the file descriptor of the
   other end of the pipe as *fdP.
-----------------------------------------------------------------------------*/
    int pipeToFeed[2];
    pid_t rc;

    pm_pipe(pipeToFeed);
    rc = fork();
    if (rc < 0) {
        pm_error("fork() of stdin feeder failed.  errno=%d (%s)", 
                 errno, strerror(errno));
    } else if (rc == 0) {
        /* This is the child -- the stdin feeder process */
        close(pipeToFeed[0]);
        (*pipeFeederRtn)(pipeToFeed[1], feederParm);
        exit(0);
    } else {
        /* This is the parent */
        pid_t const feederPid = rc;
        close(pipeToFeed[1]);
        *fdP = pipeToFeed[0];
        *pidP = feederPid;
    }
}



static void
spawnProcessor(const char *  const progName,
               const char ** const argArray,
               int           const stdinFd,
               int *         const stdoutFdP,
               pid_t *       const pidP) {
/*----------------------------------------------------------------------------
   Create a process to run program 'progName' with arguments
   argArray[] (terminated by NULL element).  Pass file descriptor
   'stdinFd' to the process as Standard Input.

   if 'stdoutFdP' is NULL, have that process write its Standard Output to
   the current process' Standard Output.

   If 'stdoutFdP' is non-NULL, set up a pipe and pass it to the new
   process as Standard Output.  Return as *stdoutFdP the file
   descriptor of the other end of that pipe, from which Caller can
   suck the program's Standard Output.
-----------------------------------------------------------------------------*/
    bool const pipeStdout = !!stdoutFdP;

    int stdoutpipe[2];
    pid_t rc;

    if (pipeStdout)
        pm_pipe(stdoutpipe);

    rc = fork();
    if (rc < 0) {
        pm_error("fork() of processor process failed.  errno=%d (%s)", 
                 errno, strerror(errno));
    } else if (rc == 0) {
        /* The program child */

        int stdoutFd;
        
        if (pipeStdout) {
            close(stdoutpipe[0]);
            stdoutFd = stdoutpipe[1];
        } else
            stdoutFd = STDOUT;

        closeUninheritableFds(stdinFd, stdoutFd);

        execProgram(progName, argArray, stdinFd, stdoutFd);

        close(stdinFd);
        close(stdoutpipe[1]);
        pm_error("INTERNAL ERROR: execProgram() returns.");
    } else {
        /* The parent */
        pid_t const processorpid = rc;

        if (pipeStdout) {
            close(stdoutpipe[1]);
            *stdoutFdP = stdoutpipe[0];
        }
        *pidP = processorpid;
    }
}



static const char *
signalName(unsigned int const signalClass) {

/* There are various signal classes that are not universally defined,
   so we make a half-hearted attempt to determine whether they are and
   not try to recognize the ones that aren't.  We do this by testing
   whether a macro is defind with the signal class name.  That could give
   a false negative, because the signal class name isn't necessarily
   defined as a macro, but it's a really, really small problem to miss
   one of these signal classes here, so we don't bother with all the work
   it would take to do it right.

   OpenBSD does not have SIGWINCH and SIGIO in 2013.  Everyone else seems
   to have them.  OpenBSD does have them if the code is not declared as
   X/open code (i.e. OpenBSD seems to interpret _XOPEN_SOURCE backward -
   it removes features rather than adds them).
*/
    switch (signalClass) {
    case SIGHUP: /* POSIX.1 */
        return "SIGHUP";
    case SIGINT: /* POSIX.1 */
        return "SIGINT";
    case SIGQUIT: /* POSIX.1 */
        return "SIGQUIT";
    case SIGILL: /* POSIX.1 */
        return "SIGILL";
    case SIGTRAP:
        return "SIGTRAP";
    case SIGABRT: /* POSIX.1 */
        return "SIGABRT";
    case SIGBUS:
        return "SIGBUS";
    case SIGFPE: /* POSIX.1 */
        return "SIGFPE";
    case SIGKILL: /* POSIX.1 */
        return "SIGKILL";
    case SIGUSR1: /* POSIX.1 */
        return "SIGUSR1";
    case SIGSEGV: /* POSIX.1 */
        return "SIGSEGV";
    case SIGUSR2: /* POSIX.1 */
        return "SIGUSR2";
    case SIGPIPE: /* POSIX.1 */
        return "SIGPIPE";
    case SIGALRM: /* POSIX.1 */
        return "SIGALRM";
    case SIGTERM: /* POSIX.1 */
        return "SIGTERM";
    case SIGCHLD: /* POSIX.1 */
        return "SIGCHLD";
    case SIGCONT: /* POSIX.1 */
        return "SIGCONT";
    case SIGSTOP: /* POSIX.1 */
        return "SIGSTOP";
    case SIGTSTP: /* POSIX.1 */
        return "SIGTSTP";
    case SIGTTIN: /* POSIX.1 */
        return "SIGTTIN";
    case SIGTTOU: /* POSIX.1 */
        return "SIGTTOU";
#ifdef SIGURG
/* SCO Openserver 5.0.7/3.2 does not have SIGURG */
    case SIGURG:
        return "SIGURG";
#endif
    case SIGXCPU:
        return "SIGXCPU";
    case SIGXFSZ:
        return "SIGXFSZ";
    case SIGVTALRM:
        return "SIGVTALRM";
    case SIGPROF:
        return "SIGPROF";
#ifdef SIGWINCH
    case SIGWINCH:
        return "SIGWINCH";
#endif
#ifdef SIGIO
/* SCO Openserver 5.0.7/3.2 does not have SIGIO */
    case SIGIO:
        return "SIGIO";
#endif
#ifdef SIGPWR
    case SIGPWR:
        return "SIGPWR";
#endif
    case SIGSYS:
        return "SIGSYS";
    default:
        return "???";
    }
}



const char *
pm_termStatusDesc(int const termStatusArg) {
/*----------------------------------------------------------------------------
   English description of  process termination status 'termStatus'.
-----------------------------------------------------------------------------*/
    const char * retval;

    /* WIFEXITED, etc. do not work with a constant argument in older GNU C
       library.  Compilation fails with "attempt to assign read-only
       location".  This is because The GNU C library has some magic to allow
       for a BSD 'union wait' (instead of int) argument to WIFEXITED.  The
       magic involves defining a variable with 'typeof' the argument and
       assigning to that variable.
       
       To work around this, we make sure the argument is not constant.
    */

    int termStatus = termStatusArg;

    if (WIFEXITED(termStatus)) {
        int const exitStatus = WEXITSTATUS(termStatus);

        if (exitStatus == 0)
            pm_asprintf(&retval, "Process exited normally");
        else
            pm_asprintf(&retval,
                        "Process exited with abnormal exit status %u.  ",
                        exitStatus);
    } else if (WIFSIGNALED(termStatus)) {
        pm_asprintf(&retval, "Process was killed by a Class %u (%s) signal.",
                    WTERMSIG(termStatus),
                    signalName(WTERMSIG(termStatus)));
    } else {
        pm_asprintf(&retval, "Process died, but its termination status "
                    "0x%x  doesn't make sense", termStatus);
    }

    return retval;
}



static void
cleanupFeederProcess(pid_t const feederPid) {
    int status;

    waitpid(feederPid, &status, 0);

    if (WIFSIGNALED(status)) {
        if (WTERMSIG(status) == SIGPIPE)
            pm_message("WARNING: "
                       "Standard Input feeder process was terminated by a "
                       "SIGPIPE signal because the program closed its "
                       "Standard Input before the Standard Input feeder was "
                       "through feeding it.");
        else
            pm_message("WARNING: "
                       "Standard Input feeder was terminated by a Signal %d.",
                       WTERMSIG(status));
    }
    else if (WIFEXITED(status)) {
        if (WEXITSTATUS(status) != 0)
            pm_message("WARNING: "
                       "Standard Input feeder process ended abnormally.  "
                       "exit status = %d", WEXITSTATUS(status));
    } else
        pm_message("WARNING: "
                   "Unrecognized process completion status from "
                   "Standard Input feeder: %d", status);
}



void
pm_system2_vp(const char *    const progName,
              const char **   const argArray,
              void stdinFeeder(int, void *),
              void *          const feederParm,
              void stdoutAccepter(int, void *),
              void *          const accepterParm,
              int *           const termStatusP) {
/*----------------------------------------------------------------------------
   Run a program in a child process.  Feed its Standard Input with a
   pipe, which is fed by the routine 'stdinFeeder' with parameter
   'feederParm'.  Process its Standard Output with the routine
   'stdoutAccepter' with parameter 'accepterParm'.

   But if 'stdinFeeder' is NULL, just feed the program our own Standard
   Input.  And if 'stdoutFeeder' is NULL, just send its Standard Output
   to our own Standard Output.

   Run the program 'progName' with arguments argArray[] (terminated by NULL
   element).  That includes arg0.

   Return as *termStatusP the termination status of the processor process
   (the one running the program named 'progName').
-----------------------------------------------------------------------------*/
    /* If 'stdinFeeder' is non-NULL, we create a child process to run
       'stdinFeeder' and create a pipe from that process as the
       program's Standard Input.

       We create another child process to run the program.

       If 'stdoutFeeder' is non-NULL, we create a pipe between the
       program process and the current process and have the program
       write its Standard Output to that pipe.  The current process
       runs 'stdoutAccepter' to read the data from that pipe.
       
       But if 'stdoutFeeder' is NULL, we just tell the program process
       to write to the current process' Standard Output.

       So there are two processes when stdinFeeder is NULL and three when
       stdinFeeder is non-null.
    */
    
    int progStdinFd;
    pid_t feederPid;
    pid_t processorPid;
    int termStatus;

    if (stdinFeeder) 
        createPipeFeeder(stdinFeeder, feederParm, &progStdinFd, &feederPid);
    else {
        progStdinFd = STDIN;
        feederPid = 0;
    }

    if (stdoutAccepter) {
        int progStdoutFd;

        /* Make a child process to run the program and pipe back to us its
           Standard Output 
        */
        spawnProcessor(progName, argArray, progStdinFd, 
                       &progStdoutFd, &processorPid);

        /* The child process has cloned our 'progStdinFd'; we have no
           more use for our copy.
        */
        close(progStdinFd);
        /* Dispose of the stdout from that child */
        (*stdoutAccepter)(progStdoutFd, accepterParm);
        close(progStdoutFd);
    } else {
        /* Run a child process for the program that sends its Standard Output
           to our Standard Output
        */
        spawnProcessor(progName, argArray, progStdinFd, NULL, &processorPid);
    }

    waitpid(processorPid, &termStatus, 0);

    if (feederPid) 
        cleanupFeederProcess(feederPid);

    *termStatusP = termStatus;
}



void
pm_system2_lp(const char *    const progName,
              void stdinFeeder(int, void *),
              void *          const feederParm,
              void stdoutAccepter(int, void *),
              void *          const accepterParm,
              int *           const termStatusP,
              ...) {
/*----------------------------------------------------------------------------
  Same as pm_system_vp() except with arguments as variable arguments
  instead of an array.

  N.B. the first variable argument is the program's arg 0; the last
  variable argument must be NULL.
-----------------------------------------------------------------------------*/
    va_list args;
    bool endOfArgs;
    const char ** argArray;
    unsigned int n;

    va_start(args, termStatusP);

    endOfArgs = FALSE;
    argArray = NULL;

    for (endOfArgs = FALSE, argArray = NULL, n = 0;
         !endOfArgs;
        ) {
        const char * const arg = va_arg(args, const char *);
        
        REALLOCARRAY(argArray, n+1);

        argArray[n++] = arg;

        if (!arg)
            endOfArgs = TRUE;
    }

    va_end(args);

    pm_system2_vp(progName, argArray,
                  stdinFeeder, feederParm, stdoutAccepter, accepterParm,
                  termStatusP);

    free(argArray);
}



void
pm_system2(void stdinFeeder(int, void *),
           void *          const feederParm,
           void stdoutAccepter(int, void *),
           void *          const accepterParm,
           const char *    const shellCommand,
           int *           const termStatusP) {
/*----------------------------------------------------------------------------
   Run a shell and have it run command 'shellCommand'.  Feed its
   Standard Input with a pipe, which is fed by the routine
   'stdinFeeder' with parameter 'feederParm'.  Process its Standard
   Output with the routine 'stdoutAccepter' with parameter 'accepterParm'.

   But if 'stdinFeeder' is NULL, just feed the shell our own Standard
   Input.  And if 'stdoutFeeder' is NULL, just send its Standard Output
   to our own Standard Output.

   Return as *termStatusP the termination status of the processor process
   (the one running the program named 'progName').
-----------------------------------------------------------------------------*/
    pm_system2_lp("/bin/sh", 
                  stdinFeeder, feederParm, stdoutAccepter, accepterParm,
                  termStatusP,
                  "sh", "-c", shellCommand, NULL);
}



void
pm_system_vp(const char *    const progName,
             const char **   const argArray,
             void stdinFeeder(int, void *),
             void *          const feederParm,
             void stdoutAccepter(int, void *),
             void *          const accepterParm) {
/*----------------------------------------------------------------------------
   Same as pm_system2_vp(), except instead of returning the termination
   status, we just issue a message (pm_message) describing it.
-----------------------------------------------------------------------------*/
    int termStatus;

    pm_system2_vp(progName, argArray,
                  stdinFeeder, feederParm,
                  stdoutAccepter, accepterParm,
                  &termStatus);

    if (termStatus != 0) {
        const char * const msg = pm_termStatusDesc(termStatus);

        pm_message("%s", msg);

        pm_strfree(msg);
    }
}



void
pm_system_lp(const char *    const progName,
             void stdinFeeder(int, void *),
             void *          const feederParm,
             void stdoutAccepter(int, void *),
             void *          const accepterParm,
             ...) {
/*----------------------------------------------------------------------------
  Same as pm_system_vp() except with arguments as variable arguments
  instead of an array.

  N.B. the first variable argument is the program's arg 0; the last
  variable argument must be NULL.
-----------------------------------------------------------------------------*/
    va_list args;
    bool endOfArgs;
    const char ** argArray;
    unsigned int n;

    va_start(args, accepterParm);

    endOfArgs = FALSE;
    argArray = NULL;

    for (endOfArgs = FALSE, argArray = NULL, n = 0;
         !endOfArgs;
        ) {
        const char * const arg = va_arg(args, const char *);
        
        REALLOCARRAY(argArray, n+1);

        argArray[n++] = arg;

        if (!arg)
            endOfArgs = TRUE;
    }

    va_end(args);

    pm_system_vp(progName, argArray,
                 stdinFeeder, feederParm, stdoutAccepter, accepterParm);

    free(argArray);
}



void
pm_system(void stdinFeeder(int, void *),
          void *          const feederParm,
          void stdoutAccepter(int, void *),
          void *          const accepterParm,
          const char *    const shellCommand) {
/*----------------------------------------------------------------------------
   Same as pm_system2(), except instead of returning the termination status,
   we just issue a message (pm_message) describing it.
-----------------------------------------------------------------------------*/
    int termStatus;

    pm_system2(stdinFeeder, feederParm, stdoutAccepter, accepterParm,
               shellCommand,
               &termStatus);

    if (termStatus != 0) {
        const char * const msg = pm_termStatusDesc(termStatus);

        pm_message("%s", msg);

        pm_strfree(msg);
    }
}



void
pm_feed_null(int    const pipeToFeedFd,
             void * const feederParm) {

}



void
pm_accept_null(int    const pipetosuckFd,
               void * const accepterParm ) {

    size_t const bufferSize = 4096;

    unsigned char * buffer;

    MALLOCARRAY(buffer, bufferSize);

    if (buffer) {
        bool eof;

        for (eof = false; !eof; ) {
            ssize_t rc;

            rc = read(pipetosuckFd, buffer, bufferSize);

            if (rc < 0) {
                /* No way to report the problem; just say we're done */
                eof = true;
            } else if (rc == 0)
                /* eof */
                eof = true;
        }
        free(buffer);
    }
    close(pipetosuckFd);
}



void
pm_feed_from_memory(int    const pipeToFeedFd,
                    void * const feederParm) {

    pm_bufferDesc * const inputBufferP = feederParm;
    
    FILE * const outFileP = fdopen(pipeToFeedFd, "w");
    
    size_t bytesTransferred;

    /* The following signals (and normally kills) the process with
       SIGPIPE if the pipe does not take all 'size' bytes.
    */
    bytesTransferred = 
        fwrite(inputBufferP->buffer, 1, inputBufferP->size, outFileP);

    if (inputBufferP->bytesTransferredP)
        *(inputBufferP->bytesTransferredP) = bytesTransferred;

    fclose(outFileP);
}



void
pm_accept_to_memory(int             const pipetosuckFd,
                    void *          const accepterParm ) {

    pm_bufferDesc * const outputBufferP = accepterParm;
    
    FILE * const inFileP = fdopen(pipetosuckFd, "r");

    size_t bytesTransferred;

    bytesTransferred =
        fread(outputBufferP->buffer, 1, outputBufferP->size, inFileP);

    fclose(inFileP);

    if (outputBufferP->bytesTransferredP)
        *(outputBufferP->bytesTransferredP) = bytesTransferred;
}



