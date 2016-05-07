#ifndef PM_SYSTEM_H_INCLUDED
#define PM_SYSTEM_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif
#if 0
} /* to fake out automatic code indenters */
#endif


void
pm_system2_vp(const char *    const progName,
              const char **   const argArray,
              void stdinFeeder(int, void *),
              void *          const feederParm,
              void stdoutAccepter(int, void *),
              void *          const accepterParm,
              int *           const termStatusP);

void
pm_system2_lp(const char *    const progName,
              void stdinFeeder(int, void *),
              void *          const feederParm,
              void stdoutAccepter(int, void *),
              void *          const accepterParm,
              int *           const termStatusP,
              ...);

void
pm_system2(void                  stdinFeeder(int, void *),
           void *          const feederParm,
           void                  stdoutAccepter(int, void *),
           void *          const accepterParm,
           const char *    const shellCommand,
           int *           const termStatusP);

void
pm_system_vp(const char *    const progName,
             const char **   const argArray,
             void stdinFeeder(int, void *),
             void *          const feederParm,
             void stdoutAccepter(int, void *),
             void *          const accepterParm);

void
pm_system_lp(const char *    const progName,
             void stdinFeeder(int, void *),
             void *          const feederParm,
             void stdoutAccepter(int, void *),
             void *          const accepterParm,
             ...);

void
pm_system(void                  stdinFeeder(int, void *),
          void *          const feederParm,
          void                  stdoutAccepter(int, void *),
          void *          const accepterParm,
          const char *    const shellCommand);

const char *
pm_termStatusDesc(int const termStatus);


/* The following are Standard Input feeders and Standard Output accepters
   for pm_system() etc.
*/
void
pm_feed_null(int    const pipeToFeedFd,
             void * const feederParm);

void
pm_accept_null(int    const pipetosuckFd,
               void * const accepterParm);

struct bufferDesc {
    /* This is just a parameter for the routines below */
    unsigned int    size;
    unsigned char * buffer;
    unsigned int *  bytesTransferredP;
};


/* The struct name "bufferDesc", without the "pm" namespace, is an unfortunate
   historical accident.
*/
typedef struct bufferDesc pm_bufferDesc;

void
pm_feed_from_memory(int    const pipeToFeedFd,
                    void * const feederParm);

void
pm_accept_to_memory(int    const pipetosuckFd,
                    void * const accepterParm);

#ifdef __cplusplus
}
#endif

#endif
