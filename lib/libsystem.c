/*=============================================================================
                                 pm_system
===============================================================================
   This is the library subroutine pm_system().  It is just like Standard C
   Library system(), except that you can supply routines for it to run to
   generate the Standard Input for the executed shell command and to accept
   the Standard Output from it.  system(), by contrast, always sets up the
   current Standard Input and Standard Output as the Standard Input and
   Standard Output of the shell command.

   By Bryan Henderson, San Jose CA  2002.12.14.

   Contributed to the public domain.
=============================================================================*/
#define _XOPEN_SOURCE

#include <stdarg.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>

#include "pm_c_util.h"
#include "mallocvar.h"
#include "pm.h"
#include "pm_system.h"

#define STDIN 0
#define STDOUT 1


static void
execProgram(const char *  const progName,
            const char ** const argArray,
            int           const stdinFd,
            int           const stdoutFd) {
/*----------------------------------------------------------------------------
   Run the program 'progName' with arguments argArray[], in a child process
   with 'stdinFd' as its Standard Input and 'stdoutFd' as its
   Standard Output.

   But leave Standard Input and Standard Output as we found them.

   Note that stdinFd or stdoutFd may actually be Standard Input and
   Standard Output already.
-----------------------------------------------------------------------------*/
    int stdinSaveFd, stdoutSaveFd;
    int rc;

    /* Make stdinFd Standard Input.
       Make stdoutFd Standard Output.
    */
    stdinSaveFd = dup(STDIN);
    stdoutSaveFd = dup(STDOUT);
    
    close(STDIN);
    close(STDOUT);

    dup2(stdinFd, STDIN);
    dup2(stdoutFd, STDOUT);

    rc = execvp(progName, (char **)argArray);

    close(STDIN);
    close(STDOUT);
    dup2(stdinSaveFd, STDIN);
    dup2(stdoutSaveFd, STDOUT);
    close(stdinSaveFd);
    close(stdoutSaveFd);

    if (rc < 0)
        pm_error("Unable to exec '%s' "
                 "(i.e. the program did not run at all).  "
                 "execvp() errno=%d (%s)",
                 progName, errno, strerror(errno));
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

    pipe(pipeToFeed);
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
   'stdinFd' to the shell as Standard Input.

   if 'stdoutFdP' is NULL, have that process write its Standard Output to
   the current process' Standard Output.

   If 'stdoutFdP' is non-NULL, set up a pipe and pass it to the new
   process as Standard Output.  Return as *stdoutFdP the file
   descriptor of the other end of that pipe, from which Caller can
   suck the program's Standard Output.
-----------------------------------------------------------------------------*/
    bool const pipeStdout = !stdoutFdP;
    int stdoutpipe[2];
    pid_t rc;

    if (pipeStdout)
        pipe(stdoutpipe);

    rc = fork();
    if (rc < 0) {
        pm_error("fork() of processor process failed.  errno=%d (%s)\n", 
                 errno, strerror(errno));
    } else if (rc == 0) {
        /* The program child */

        int stdoutFd;
        
        if (pipeStdout) {
            close(stdoutpipe[0]);
            stdoutFd = stdoutpipe[1];
        } else
            stdoutFd = STDOUT;

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


static void
cleanupProcessorProcess(pid_t const processorPid) {

    int status;
    waitpid(processorPid, &status, 0);
    if (status != 0) 
        pm_message("Shell process ended abnormally.  "
                   "completion code = %d", status);
}



static void
cleanupFeederProcess(pid_t const feederPid) {
    int status;

    waitpid(feederPid, &status, 0);

    if (WIFSIGNALED(status)) {
        if (WTERMSIG(status) == SIGPIPE)
            pm_message("WARNING: "
                       "Standard Input feeder process was terminated by a "
                       "SIGPIPE signal because the shell command closed its "
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
pm_system_vp(const char *    const progName,
             const char **   const argArray,
             void stdinFeeder(int, void *),
             void *          const feederParm,
             void stdoutAccepter(int, void *),
             void *          const accepterParm) {
/*----------------------------------------------------------------------------
   Run a program in a child process.  Feed its Standard Input with a
   pipe, which is fed by the routine 'stdinFeeder' with parameter
   'feederParm'.  Process its Standard Output with the routine
   'stdoutAccepter' with parameter 'accepterParm'.

   But if 'stdinFeeder' is NULL, just feed the program our own Standard
   Input.  And if 'stdoutFeeder' is NULL, just send its Standard Output
   to our own Standard Output.
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
        spawnProcessor(progName, argArray, STDIN, NULL, &processorPid);
    }

    cleanupProcessorProcess(processorPid);

    if (feederPid) 
        cleanupFeederProcess(feederPid);
}



void
pm_system_lp(const char *    const progName,
             void stdinFeeder(int, void *),
             void *          const feederParm,
             void stdoutAccepter(int, void *),
             void *          const accepterParm,
             ...) {
/*----------------------------------------------------------------------------
  same as pm_system_vp() except with arguments as variable arguments
  instead of an array.
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
   Run a shell and have it run command 'shellCommand'.  Feed its
   Standard Input with a pipe, which is fed by the routine
   'stdinFeeder' with parameter 'feederParm'.  Process its Standard
   Output with the routine 'stdoutAccepter' with parameter 'accepterParm'.

   But if 'stdinFeeder' is NULL, just feed the shell our own Standard
   Input.  And if 'stdoutFeeder' is NULL, just send its Standard Output
   to our own Standard Output.
-----------------------------------------------------------------------------*/

    pm_system_lp("/bin/sh", 
                 stdinFeeder, feederParm, stdoutAccepter, accepterParm,
                 "sh", "-c", shellCommand, NULL);
}



void
pm_feed_from_memory(int    const pipeToFeedFd,
                    void * const feederParm) {

    struct bufferDesc * const inputBufferP = feederParm;
    
    FILE * const outfile = fdopen(pipeToFeedFd, "w");
    
    int bytesTransferred;

    /* The following signals (and normally kills) the process with
       SIGPIPE if the pipe does not take all 'size' bytes.
    */
    bytesTransferred = 
        fwrite(inputBufferP->buffer, 1, inputBufferP->size, outfile);

    if (inputBufferP->bytesTransferredP)
        *(inputBufferP->bytesTransferredP) = bytesTransferred;

    fclose(outfile);
}



void
pm_accept_to_memory(int             const pipetosuckFd,
                    void *          const accepterParm ) {

    struct bufferDesc * const outputBufferP = accepterParm;
    
    FILE * const infile = fdopen(pipetosuckFd, "r");

    int bytesTransferred;

    bytesTransferred =
        fread(outputBufferP->buffer, 1, outputBufferP->size, infile);

    fclose(infile);

    if (outputBufferP->bytesTransferredP)
        *(outputBufferP->bytesTransferredP) = bytesTransferred;
}
